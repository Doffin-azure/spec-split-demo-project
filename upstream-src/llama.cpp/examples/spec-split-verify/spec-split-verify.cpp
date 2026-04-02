#include "common.h"
#include "sampling.h"
#include "llama.h"
#include "log.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::ordered_json;
namespace fs = std::filesystem;

struct app_args {
    std::string model_path;
    std::string shared_dir;
    std::string prompt;
    int32_t max_output_tokens = 64;
    int32_t poll_ms = 200;
    int32_t n_ctx = 512;
    int32_t n_batch = 512;
    int32_t n_ubatch = 512;
    int32_t n_gpu_layers = 99;
};

static void usage(const char * prog) {
    std::fprintf(stderr,
        "usage: %s --model <gguf> --shared-dir <dir> --prompt <text> [options]\n"
        "options:\n"
        "  --max-output-tokens N   default 64\n"
        "  --poll-ms N             default 200\n"
        "  --ctx-size N            default 512\n"
        "  --batch-size N          default 512\n"
        "  --ubatch-size N         default 512\n"
        "  --gpu-layers N          default 99\n",
        prog);
}

static bool parse_args(int argc, char ** argv, app_args & out) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need = [&](const char * name) -> const char * {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                std::exit(1);
            }
            return argv[++i];
        };

        if (arg == "--model" || arg == "-m") {
            out.model_path = need("--model");
        } else if (arg == "--shared-dir") {
            out.shared_dir = need("--shared-dir");
        } else if (arg == "--prompt" || arg == "-p") {
            out.prompt = need("--prompt");
        } else if (arg == "--max-output-tokens") {
            out.max_output_tokens = std::stoi(need("--max-output-tokens"));
        } else if (arg == "--poll-ms") {
            out.poll_ms = std::stoi(need("--poll-ms"));
        } else if (arg == "--ctx-size" || arg == "-c") {
            out.n_ctx = std::stoi(need("--ctx-size"));
        } else if (arg == "--batch-size" || arg == "-b") {
            out.n_batch = std::stoi(need("--batch-size"));
        } else if (arg == "--ubatch-size" || arg == "-ub") {
            out.n_ubatch = std::stoi(need("--ubatch-size"));
        } else if (arg == "--gpu-layers" || arg == "-ngl") {
            out.n_gpu_layers = std::stoi(need("--gpu-layers"));
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return false;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", arg.c_str());
            usage(argv[0]);
            return false;
        }
    }

    if (out.model_path.empty() || out.shared_dir.empty() || out.prompt.empty()) {
        usage(argv[0]);
        return false;
    }
    return true;
}

static json read_json(const fs::path & p) {
    std::ifstream f(p);
    if (!f.is_open()) {
        return json();
    }
    json j;
    f >> j;
    return j;
}

static void write_json(const fs::path & p, const json & j) {
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp);
        f << j.dump(2);
    }
    fs::rename(tmp, p);
}

static void append_text(const fs::path & p, const std::string & s) {
    if (s.empty()) {
        return;
    }
    std::ofstream f(p, std::ios::app);
    f << s;
}

static int64_t now_us() {
    return ggml_time_us();
}

int main(int argc, char ** argv) {
    std::setlocale(LC_NUMERIC, "C");
    common_init();

    app_args args;
    if (!parse_args(argc, argv, args)) {
        return 1;
    }

    const fs::path shared = args.shared_dir;
    const fs::path state_path = shared / "state.json";
    const fs::path proposal_path = shared / "proposal.json";
    const fs::path decision_path = shared / "decision.json";
    const fs::path document_path = shared / "document.md";
    const fs::path config_path = shared / "config.json";

    llama_backend_init();

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = args.n_gpu_layers;

    llama_model * model = llama_model_load_from_file(args.model_path.c_str(), mparams);
    if (!model) {
        LOG_ERR("failed to load model: %s\n", args.model_path.c_str());
        return 1;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = args.n_ctx;
    cparams.n_batch = args.n_batch;
    cparams.n_ubatch = args.n_ubatch;
    cparams.no_perf = false;

    llama_context * ctx = llama_init_from_model(model, cparams);
    if (!ctx) {
        LOG_ERR("failed to init context\n");
        llama_model_free(model);
        return 1;
    }

    const llama_vocab * vocab = llama_model_get_vocab(model);

    common_params_sampling sparams;
    sparams.temp = 0.0f;
    sparams.top_k = 1;
    sparams.top_p = 1.0f;
    sparams.min_p = 0.0f;
    sparams.penalty_repeat = 1.0f;
    sparams.penalty_freq = 0.0f;
    sparams.penalty_present = 0.0f;
    sparams.no_perf = false;
    struct common_sampler * smpl = common_sampler_init(model, sparams);

    llama_tokens prompt_tokens = common_tokenize(ctx, args.prompt, true, true);
    if (prompt_tokens.size() < 2) {
        LOG_ERR("prompt tokenized too short\n");
        common_sampler_free(smpl);
        llama_free(ctx);
        llama_model_free(model);
        return 1;
    }

    // Initial state reconstruction from state.json.
    json state = read_json(state_path);
    std::vector<llama_token> accepted_ids;
    if (state.contains("accepted_token_ids") && state["accepted_token_ids"].is_array()) {
        for (const auto & el : state["accepted_token_ids"]) {
            accepted_ids.push_back(el.get<llama_token>());
        }
    }

    llama_tokens seq = prompt_tokens;
    seq.insert(seq.end(), accepted_ids.begin(), accepted_ids.end());
    if (seq.size() < 2) {
        LOG_ERR("invalid reconstructed sequence\n");
        common_sampler_free(smpl);
        llama_free(ctx);
        llama_model_free(model);
        return 1;
    }

    llama_tokens prompt_tgt(seq.begin(), seq.end() - 1);
    llama_token id_last = seq.back();

    if (llama_n_ctx(ctx) < prompt_tgt.size() + 1) {
        LOG_ERR("sequence too long for ctx: %zu >= %u\n", prompt_tgt.size() + 1, llama_n_ctx(ctx));
        common_sampler_free(smpl);
        llama_free(ctx);
        llama_model_free(model);
        return 1;
    }

    // Build sampler history from full known sequence.
    common_sampler_reset(smpl);
    for (llama_token t : seq) {
        common_sampler_accept(smpl, t, false);
    }

    // Evaluate prompt_tgt into KV (id_last is intentionally excluded).
    if (llama_decode(ctx, llama_batch_get_one(prompt_tgt.data(), (int32_t) prompt_tgt.size())) != 0) {
        LOG_ERR("failed to decode reconstructed prompt\n");
        common_sampler_free(smpl);
        llama_free(ctx);
        llama_model_free(model);
        return 1;
    }

    int n_past = (int) prompt_tgt.size();
    llama_batch batch_tgt = llama_batch_init(std::max(32, args.n_batch), 0, 1);

    LOG_INF("[verify-native] started, shared_dir=%s\n", args.shared_dir.c_str());

    while (true) {
        const int64_t t_loop_us = now_us();
        state = read_json(state_path);
        json config = read_json(config_path);
        if (state.is_null() || config.is_null()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(args.poll_ms));
            continue;
        }

        if (state.value("done", false)) {
            LOG_INF("[verify-native] done=true, exit\n");
            break;
        }

        const int round_id = state.value("round", 0);
        const int accepted_pos = state.value("accepted_pos", 0);
        const int max_output_tokens = config.value("max_output_tokens", args.max_output_tokens);

        json proposal = read_json(proposal_path);
        if (proposal.is_null()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(args.poll_ms));
            continue;
        }
        if (proposal.value("round", -1) != round_id || proposal.value("accepted_pos", -1) != accepted_pos) {
            std::this_thread::sleep_for(std::chrono::milliseconds(args.poll_ms));
            continue;
        }
        const int64_t t_proposal_seen_us = now_us();

        std::vector<llama_token> draft_ids;
        if (proposal.contains("draft_token_ids") && proposal["draft_token_ids"].is_array()) {
            for (const auto & el : proposal["draft_token_ids"]) {
                draft_ids.push_back(el.get<llama_token>());
            }
        }
        int64_t proposal_to_verify_seen_us = -1;
        if (proposal.contains("timing") && proposal["timing"].is_object()) {
            const auto & tim = proposal["timing"];
            if (tim.contains("proposal_write_us")) {
                const int64_t t_prop_write_us = tim["proposal_write_us"].get<int64_t>();
                proposal_to_verify_seen_us = t_proposal_seen_us - t_prop_write_us;
            }
        }

        // Evaluate [id_last, draft...].
        const int64_t t_decode_begin_us = now_us();
        common_batch_clear(batch_tgt);
        common_batch_add(batch_tgt, id_last, n_past++, { 0 }, true);
        for (size_t i = 0; i < draft_ids.size(); ++i) {
            common_batch_add(batch_tgt, draft_ids[i], n_past + (int) i, { 0 }, true);
        }

        if (llama_decode(ctx, batch_tgt) != 0) {
            LOG_ERR("[verify-native] llama_decode failed at round %d\n", round_id);
            std::this_thread::sleep_for(std::chrono::milliseconds(args.poll_ms));
            continue;
        }
        const int64_t t_decode_end_us = now_us();

        const int64_t t_sample_begin_us = now_us();
        const auto ids = common_sampler_sample_and_accept_n(smpl, ctx, draft_ids);
        if (ids.empty()) {
            LOG_ERR("[verify-native] empty ids at round %d\n", round_id);
            std::this_thread::sleep_for(std::chrono::milliseconds(args.poll_ms));
            continue;
        }
        const int64_t t_sample_end_us = now_us();

        n_past += (int) ids.size() - 1;
        const int accepted_draft = std::max(0, (int) ids.size() - 1);

        // Update prompt_tgt/id_last exactly like speculative-simple.
        for (llama_token id : ids) {
            prompt_tgt.push_back(id_last);
            id_last = id;
            accepted_ids.push_back(id);
        }

        // Rollback non-accepted speculative tail in target KV.
        const int64_t t_rollback_begin_us = now_us();
        llama_memory_seq_rm(llama_get_memory(ctx), 0, n_past, -1);
        const int64_t t_rollback_end_us = now_us();

        const std::string result_text = common_detokenize(ctx, ids, false);
        append_text(document_path, result_text);

        bool has_eos = llama_vocab_is_eog(vocab, id_last);
        const bool done = has_eos || ((int) accepted_ids.size() >= max_output_tokens);
        const int new_accepted_pos = accepted_pos + (int) ids.size();
        const int64_t t_decision_write_us = now_us();
        const bool had_reject = accepted_draft < (int) draft_ids.size();

        json decision = {
            {"round", round_id},
            {"accepted_draft", accepted_draft},
            {"result_tokens", json::array()},
            {"result_token_ids", ids},
            {"result_text", result_text},
            {"new_accepted_pos", new_accepted_pos},
            {"done", done},
            {"exhausted", false},
            {"timing", {
                {"loop_start_us", t_loop_us},
                {"proposal_seen_us", t_proposal_seen_us},
                {"proposal_to_verify_seen_us", proposal_to_verify_seen_us},
                {"decode_begin_us", t_decode_begin_us},
                {"decode_end_us", t_decode_end_us},
                {"sample_begin_us", t_sample_begin_us},
                {"sample_end_us", t_sample_end_us},
                {"rollback_begin_us", t_rollback_begin_us},
                {"rollback_end_us", t_rollback_end_us},
                {"decision_write_us", t_decision_write_us},
                {"had_reject", had_reject},
                {"draft_count", (int) draft_ids.size()},
                {"accepted_draft", accepted_draft},
                {"emitted_count", (int) ids.size()},
            }},
        };
        write_json(decision_path, decision);

        state["accepted_pos"] = new_accepted_pos;
        state["round"] = round_id + 1;
        state["done"] = done;
        state["accepted_token_ids"] = accepted_ids;
        write_json(state_path, state);

        std::error_code ec;
        fs::remove(proposal_path, ec);

        LOG_INF("[verify-native][timing] round=%d draft=%zu accept=%d reject=%d comm=%.3fms decode=%.3fms sample=%.3fms rollback=%.3fms\n",
                round_id,
                draft_ids.size(),
                accepted_draft,
                had_reject ? 1 : 0,
                proposal_to_verify_seen_us >= 0 ? proposal_to_verify_seen_us / 1000.0 : -1.0,
                (t_decode_end_us - t_decode_begin_us) / 1000.0,
                (t_sample_end_us - t_sample_begin_us) / 1000.0,
                (t_rollback_end_us - t_rollback_begin_us) / 1000.0);
        if (done) {
            LOG_INF("[verify-native] target stream fully emitted\n");
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(args.poll_ms));
    }

    llama_batch_free(batch_tgt);
    common_sampler_free(smpl);
    llama_free(ctx);
    llama_model_free(model);
    return 0;
}
