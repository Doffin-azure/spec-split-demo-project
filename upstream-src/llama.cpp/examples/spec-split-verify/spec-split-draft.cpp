#include "common.h"
#include "sampling.h"
#include "llama.h"
#include "log.h"

#include <nlohmann/json.hpp>

#include <chrono>
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
    int32_t n_max = 4;
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
        "  --n-max N         default 4\n"
        "  --poll-ms N       default 200\n"
        "  --ctx-size N      default 512\n"
        "  --batch-size N    default 512\n"
        "  --ubatch-size N   default 512\n"
        "  --gpu-layers N    default 99\n",
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
        } else if (arg == "--n-max") {
            out.n_max = std::stoi(need("--n-max"));
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

static size_t lcp_len(const std::vector<llama_token> & a, const std::vector<llama_token> & b) {
    const size_t n = std::min(a.size(), b.size());
    size_t i = 0;
    while (i < n && a[i] == b[i]) {
        ++i;
    }
    return i;
}

static bool decode_append(
        llama_context * ctx,
        llama_batch & batch,
        std::vector<llama_token> & seq,
        const std::vector<llama_token> & toks) {
    if (toks.empty()) {
        return true;
    }
    common_batch_clear(batch);
    const int n0 = (int) seq.size();
    for (size_t i = 0; i < toks.size(); ++i) {
        const bool logits = (i + 1 == toks.size());
        common_batch_add(batch, toks[i], n0 + (int) i, { 0 }, logits);
    }
    if (llama_decode(ctx, batch) != 0) {
        return false;
    }
    seq.insert(seq.end(), toks.begin(), toks.end());
    return true;
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

    const llama_tokens prompt_tokens = common_tokenize(ctx, args.prompt, true, true);
    if (prompt_tokens.empty()) {
        LOG_ERR("prompt tokenized empty\n");
        common_sampler_free(smpl);
        llama_free(ctx);
        llama_model_free(model);
        return 1;
    }

    llama_batch batch = llama_batch_init(std::max(32, args.n_batch), 0, 1);
    std::vector<llama_token> prompt_dft; // includes prompt + accepted + speculative tail

    LOG_INF("[draft-native] started, shared_dir=%s\n", args.shared_dir.c_str());

    while (true) {
        const int64_t t_loop_us = now_us();
        json state = read_json(state_path);
        json config = read_json(config_path);
        if (state.is_null() || config.is_null()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(args.poll_ms));
            continue;
        }
        if (state.value("done", false)) {
            LOG_INF("[draft-native] done=true, exit\n");
            break;
        }

        if (state.value("mode", std::string("toy")) != "model") {
            std::this_thread::sleep_for(std::chrono::milliseconds(args.poll_ms));
            continue;
        }

        const int round_id = state.value("round", 0);
        const int accepted_pos = state.value("accepted_pos", 0);
        const int n_max = config.value("n_max", args.n_max);
        const int64_t t_state_seen_us = now_us();

        json cur_prop = read_json(proposal_path);
        if (!cur_prop.is_null() && cur_prop.value("round", -1) == round_id) {
            std::this_thread::sleep_for(std::chrono::milliseconds(args.poll_ms));
            continue;
        }

        int64_t t_decision_seen_us = 0;
        int64_t decision_to_draft_seen_us = -1;
        const json prev_decision = read_json(decision_path);
        if (!prev_decision.is_null() && prev_decision.value("round", -1) == round_id - 1) {
            t_decision_seen_us = now_us();
            if (prev_decision.contains("timing") && prev_decision["timing"].is_object()) {
                const auto & tim = prev_decision["timing"];
                if (tim.contains("decision_write_us")) {
                    const int64_t t_write_us = tim["decision_write_us"].get<int64_t>();
                    decision_to_draft_seen_us = t_decision_seen_us - t_write_us;
                }
            }
        }

        // Authoritative accepted sequence from verify side.
        std::vector<llama_token> accepted_ids;
        if (state.contains("accepted_token_ids") && state["accepted_token_ids"].is_array()) {
            for (const auto & el : state["accepted_token_ids"]) {
                accepted_ids.push_back(el.get<llama_token>());
            }
        }
        std::vector<llama_token> target_seq(prompt_tokens.begin(), prompt_tokens.end());
        target_seq.insert(target_seq.end(), accepted_ids.begin(), accepted_ids.end());

        // Sync local draft context to authoritative sequence.
        const int64_t t_sync_begin_us = now_us();
        bool did_rollback = false;
        bool did_hard_reset = false;
        const size_t cp = lcp_len(prompt_dft, target_seq);
        const bool mismatch_inside_prefix = cp < std::min(prompt_dft.size(), target_seq.size());

        if (mismatch_inside_prefix) {
            // Hard reset if prefix diverged.
            did_hard_reset = true;
            llama_memory_clear(llama_get_memory(ctx), false);
            prompt_dft.clear();
            if (!decode_append(ctx, batch, prompt_dft, target_seq)) {
                LOG_ERR("[draft-native] failed to rebuild context at round %d\n", round_id);
                std::this_thread::sleep_for(std::chrono::milliseconds(args.poll_ms));
                continue;
            }
        } else {
            if (prompt_dft.size() > target_seq.size()) {
                // Rollback speculative tail not confirmed by verifier.
                did_rollback = true;
                llama_memory_seq_rm(llama_get_memory(ctx), 0, (llama_pos) target_seq.size(), -1);
                prompt_dft.resize(target_seq.size());
            }
            if (prompt_dft.size() < target_seq.size()) {
                std::vector<llama_token> missing(
                    target_seq.begin() + (long) prompt_dft.size(),
                    target_seq.end());
                if (!decode_append(ctx, batch, prompt_dft, missing)) {
                    LOG_ERR("[draft-native] failed to extend context at round %d\n", round_id);
                    std::this_thread::sleep_for(std::chrono::milliseconds(args.poll_ms));
                    continue;
                }
            }
        }
        const int64_t t_sync_end_us = now_us();

        if (prompt_dft.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(args.poll_ms));
            continue;
        }

        // Ensure next-token logits align to the current tail token after any rollback.
        const int64_t t_tail_refresh_begin_us = now_us();
        {
            const llama_token tail = prompt_dft.back();
            llama_memory_seq_rm(llama_get_memory(ctx), 0, (llama_pos) (prompt_dft.size() - 1), -1);
            prompt_dft.pop_back();
            std::vector<llama_token> one = { tail };
            if (!decode_append(ctx, batch, prompt_dft, one)) {
                LOG_ERR("[draft-native] failed to refresh tail logits at round %d\n", round_id);
                std::this_thread::sleep_for(std::chrono::milliseconds(args.poll_ms));
                continue;
            }
        }
        const int64_t t_tail_refresh_end_us = now_us();

        // Draft generation (greedy) and keep speculative tail in prompt_dft.
        const int64_t t_draft_begin_us = now_us();
        common_sampler_reset(smpl);
        for (llama_token t : prompt_dft) {
            common_sampler_accept(smpl, t, false);
        }

        std::vector<llama_token> draft_ids;
        draft_ids.reserve((size_t) std::max(0, n_max));
        for (int i = 0; i < n_max; ++i) {
            const llama_token id = common_sampler_sample(smpl, ctx, 0, true);
            common_sampler_accept(smpl, id, true);
            draft_ids.push_back(id);

            std::vector<llama_token> one = { id };
            if (!decode_append(ctx, batch, prompt_dft, one)) {
                LOG_ERR("[draft-native] failed while decoding drafted token at round %d\n", round_id);
                break;
            }
        }
        const int64_t t_draft_end_us = now_us();

        const std::string draft_text = common_detokenize(ctx, draft_ids, false);
        const int id_last = accepted_ids.empty() ? -1 : accepted_ids.back();
        const int64_t t_proposal_write_us = now_us();

        json proposal = {
            {"round", round_id},
            {"accepted_pos", accepted_pos},
            {"id_last", id_last},
            {"draft_token_ids", draft_ids},
            {"draft_text", draft_text},
            {"n_max", n_max},
            {"timing", {
                {"loop_start_us", t_loop_us},
                {"state_seen_us", t_state_seen_us},
                {"decision_seen_us", t_decision_seen_us},
                {"decision_to_draft_seen_us", decision_to_draft_seen_us},
                {"sync_begin_us", t_sync_begin_us},
                {"sync_end_us", t_sync_end_us},
                {"tail_refresh_begin_us", t_tail_refresh_begin_us},
                {"tail_refresh_end_us", t_tail_refresh_end_us},
                {"draft_begin_us", t_draft_begin_us},
                {"draft_end_us", t_draft_end_us},
                {"proposal_write_us", t_proposal_write_us},
                {"did_rollback", did_rollback},
                {"did_hard_reset", did_hard_reset},
            }},
        };
        write_json(proposal_path, proposal);

        LOG_INF("[draft-native][timing] round=%d drafted=%zu sync=%.3fms tail=%.3fms draft=%.3fms decision->draft=%.3fms rollback=%d hard_reset=%d\n",
                round_id,
                draft_ids.size(),
                (t_sync_end_us - t_sync_begin_us) / 1000.0,
                (t_tail_refresh_end_us - t_tail_refresh_begin_us) / 1000.0,
                (t_draft_end_us - t_draft_begin_us) / 1000.0,
                decision_to_draft_seen_us >= 0 ? decision_to_draft_seen_us / 1000.0 : -1.0,
                did_rollback ? 1 : 0,
                did_hard_reset ? 1 : 0);

        std::this_thread::sleep_for(std::chrono::milliseconds(args.poll_ms));
    }

    llama_batch_free(batch);
    common_sampler_free(smpl);
    llama_free(ctx);
    llama_model_free(model);
    return 0;
}
