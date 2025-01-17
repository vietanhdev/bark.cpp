/*
Port of Suno's Bark to C/C++.

Author: Pierre-Antoine Bannier <pierreantoine.bannier@gmail.com>
*/
#include "bark.h"
#include "ggml.h"
#include "bark-util.h"

// third-party utilities
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <regex>
#include <string>

#define BARK_DEBUG 0

bool allequal(struct ggml_tensor * a, struct ggml_tensor * b, std::string test_name) {
    assert(a->ne[0] == b->ne[0]);
    assert(a->ne[1] == b->ne[1]);
    assert(a->ne[2] == b->ne[2]);
    assert(a->ne[3] == b->ne[3]);

    assert(a->type == GGML_TYPE_I32);
    assert(b->type == GGML_TYPE_I32);

    int64_t n_violations = 0;

    for (int i = 0; i < a->ne[3]; i++) {
        for (int j = 0; j < a->ne[2]; j++) {
            for (int k = 0; k < a->ne[1]; k++) {
                for (int l = 0; l < a->ne[0]; l++) {
                    int32_t * aval = (int32_t *) (
                        (char *) a->data + i*a->nb[3] + j*a->nb[2] + k*a->nb[1] + l*a->nb[0]);
                    int32_t * bval = (int32_t *) (
                        (char *) b->data + i*b->nb[3] + j*b->nb[2] + k*b->nb[1] + l*b->nb[0]);
                    if (*aval != *bval)
                        n_violations += 1;
                }
            }
        }
    }

    int64_t n_elements = a->ne[0]*a->ne[1]*a->ne[2]*a->ne[3];
    float perc_viol = 100.0f*float(n_violations)/n_elements;

    printf("%s: %s\n", __func__, test_name.c_str());
    printf("%s: %%_viol=%.1f\n", __func__, perc_viol);
    printf("\n");
    return n_violations == 0;
}

bool allclose(struct ggml_tensor * a, struct ggml_tensor * b, float tol, std::string test_name) {
    assert(a->ne[0] == b->ne[0]);
    assert(a->ne[1] == b->ne[1]);
    assert(a->ne[2] == b->ne[2]);
    assert(a->ne[3] == b->ne[3]);

    assert(a->type == GGML_TYPE_F32);
    assert(b->type == GGML_TYPE_F32);

    float max_violation = -INFINITY;
    int64_t n_violations = 0;

    for (int i = 0; i < a->ne[3]; i++) {
        for (int j = 0; j < a->ne[2]; j++) {
            for (int k = 0; k < a->ne[1]; k++) {
                for (int l = 0; l < a->ne[0]; l++) {
                    float * aval = (float *) (
                        (char *) a->data + i*a->nb[3] + j*a->nb[2] + k*a->nb[1] + l*a->nb[0]);
                    float * bval = (float *) (
                        (char *) b->data + i*b->nb[3] + j*b->nb[2] + k*b->nb[1] + l*b->nb[0]);
                    float violation = fabs(*aval - *bval);
                    max_violation = std::max(max_violation, violation);
                    if (violation > tol)
                        n_violations += 1;
                }
            }
        }
    }

    int64_t n_elements = a->ne[0]*a->ne[1]*a->ne[2]*a->ne[3];
    float perc_viol = 100.0f*float(n_violations)/n_elements;

    printf("%s: %s\n", __func__, test_name.c_str());
    printf("%s: max_viol=%.4f; viol=%.1f%% (tol=%.4f)\n", __func__, max_violation, perc_viol, tol);
    printf("\n");
    return n_violations == 0;
}


void read_tensor_from_file_f32(std::ifstream & fin, struct ggml_tensor *t) {
    int32_t n_dims;
    read_safe(fin, n_dims);

    int32_t ne[3] = { 1, 1, 1 };
    for (int i = 0; i < n_dims; i++) { read_safe(fin, ne[i]); }

    assert(t->ne[0] == ne[0]);
    assert(t->ne[1] == ne[1]);
    assert(t->ne[2] == ne[2]);
    assert(t->type == GGML_TYPE_F32);

    for (int i = 0; i < ne[2]; i++) {
        for (int j = 0; j < ne[1]; j++) {
            int offset = i*t->nb[2] + j*t->nb[1];
            fin.read(reinterpret_cast<char *>(t->data) + offset, ne[0]*sizeof(float));
        }
    }
}

void read_tensor_from_file_int32(std::ifstream & fin, struct ggml_tensor *t) {
    int32_t n_dims;
    read_safe(fin, n_dims);

    int32_t ne[3] = { 1, 1, 1 };
    for (int i = 0; i < n_dims; i++) { read_safe(fin, ne[i]); }

    assert(t->ne[0] == ne[0]);
    assert(t->ne[1] == ne[1]);
    assert(t->ne[2] == ne[2]);
    assert(t->type == GGML_TYPE_I32);

    for (int i = 0; i < ne[2]; i++) {
        for (int j = 0; j < ne[1]; j++) {
            int offset = i*t->nb[2] + j*t->nb[1];
            fin.read(reinterpret_cast<char *>(t->data) + offset, ne[0]*sizeof(int32_t));
        }
    }
}

void read_tensor_from_file(std::ifstream & fin, struct ggml_tensor * t) {
    if (t->type == GGML_TYPE_F32) {
        read_tensor_from_file_f32(fin, t);
    } else if (t->type == GGML_TYPE_I32) {
        read_tensor_from_file_int32(fin, t);
    } else {
        throw;
    }
}

void load_gt_tensor(std::string path, struct ggml_tensor * t) {
    auto fin = std::ifstream(path, std::ios::binary);
    if (!fin) {
        fprintf(stderr, "failed to open.");
        throw;
    }
    read_tensor_from_file(fin, t);
}

void print_tensor(struct ggml_tensor * a) {
    for (int i = 0; i < a->ne[3]; i++) {
        for (int j = 0; j < a->ne[2]; j++) {
            for (int k = 0; k < a->ne[1]; k++) {
                for (int l = 0; l < a->ne[0]; l++) {
                    if (a->type == GGML_TYPE_F32) {
                        float * aval = (float *) (
                            (char *) a->data + i*a->nb[3] + j*a->nb[2] + k*a->nb[1] + l*a->nb[0]);
                        printf("%.4f ", *aval);
                    } else if (a->type == GGML_TYPE_I32) {
                        int32_t * aval = (int32_t *) (
                            (char *) a->data + i*a->nb[3] + j*a->nb[2] + k*a->nb[1] + l*a->nb[0]);
                        printf("%d ", *aval);
                    } else {
                        throw;
                    }
                }
                printf("\n");
            }
            printf("\n\n");
        }
    }
}

bool bark_vocab_load(const std::string& fname, bark_vocab& vocab, int32_t expected_size) {
    auto fin = std::ifstream(fname, std::ios::binary);
    if (!fin) {
        fprintf(stderr, "%s: faield to open '%s'\n", __func__, fname.c_str());
        return false;
    }

    // verify magic
    {
        uint32_t magic;
        fin.read((char *) &magic, sizeof(magic));
        if (magic != GGML_FILE_MAGIC) {
            fprintf(stderr, "%s: invalid model file '%s' (bad magic)\n", __func__, fname.c_str());
            return false;
        }
    }

    int32_t n_vocab;
    read_safe(fin, n_vocab);

    // 5 special tokens: [UNK, SEP, MASK, PAD, CLS]
    if (n_vocab != expected_size) {
        fprintf(stderr, "%s: wrong voculary size (%d != %d)\n", __func__, n_vocab, expected_size);
        return false;
    }

    std::string word;
    std::vector<char> tmp;

    tmp.reserve(128);

    for (int i = 0; i < n_vocab; i++) {
        uint32_t len;
        read_safe(fin, len);

        if (len > 0) {
            tmp.resize(len);
            fin.read(&tmp[0], tmp.size()); // read to buffer
            word.assign(&tmp[0], tmp.size());
        } else {
            word = "";
        }

        vocab.token_to_id[word] = i;
        vocab.id_to_token[i] = word;
    }

    return true;
}

bool gpt_model_load(const std::string& fname, gpt_model& model) {
    auto fin = std::ifstream(fname, std::ios::binary);
    if (!fin) {
        fprintf(stderr, "%s: failed to open '%s'\n", __func__, fname.c_str());
        return false;
    }

    // verify magic
    {
        uint32_t magic;
        fin.read((char *) &magic, sizeof(magic));
        if (magic != GGML_FILE_MAGIC) {
            fprintf(stderr, "%s: invalid model file '%s' (bad magic)\n", __func__, fname.c_str());
            return false;
        }
    }

    // load hparams
    {
        auto & hparams = model.hparams;

        read_safe(fin, hparams.n_layer);
        read_safe(fin, hparams.n_head);
        read_safe(fin, hparams.n_embd);
        read_safe(fin, hparams.block_size);
        read_safe(fin, hparams.n_in_vocab);
        read_safe(fin, hparams.n_out_vocab);
        read_safe(fin, hparams.n_lm_heads);
        read_safe(fin, hparams.n_wtes);
        read_safe(fin, hparams.ftype);

        const int32_t qntvr = hparams.ftype / GGML_QNT_VERSION_FACTOR;

        printf("%s: n_in_vocab  = %d\n", __func__, hparams.n_in_vocab);
        printf("%s: n_out_vocab = %d\n", __func__, hparams.n_out_vocab);
        printf("%s: block_size  = %d\n", __func__, hparams.block_size);
        printf("%s: n_embd      = %d\n", __func__, hparams.n_embd);
        printf("%s: n_head      = %d\n", __func__, hparams.n_head);
        printf("%s: n_layer     = %d\n", __func__, hparams.n_layer);
        printf("%s: n_lm_heads  = %d\n", __func__, hparams.n_lm_heads);
        printf("%s: n_wtes      = %d\n", __func__, hparams.n_wtes);
        printf("%s: ftype       = %d\n", __func__, hparams.ftype);
        printf("%s: qntvr       = %d\n", __func__, qntvr);

        hparams.ftype %= GGML_QNT_VERSION_FACTOR;
    }

    // for the big tensors, we have the option to store the data in 16-bit floats or quantized
    // in order to save memory and also to speed up the computation
    ggml_type wtype = ggml_ftype_to_ggml_type((ggml_ftype) (model.hparams.ftype));
    if (wtype == GGML_TYPE_COUNT) {
        fprintf(stderr, "%s: invalid model file '%s' (bad ftype value %d)\n",
                __func__, fname.c_str(), model.hparams.ftype);
        return false;
    }

    auto & ctx = model.ctx;

    size_t ctx_size = 0;

    {
        const auto & hparams = model.hparams;

        const int n_embd      = hparams.n_embd;
        const int n_layer     = hparams.n_layer;
        const int block_size  = hparams.block_size;
        const int n_in_vocab  = hparams.n_in_vocab;
        const int n_out_vocab = hparams.n_out_vocab;
        const int n_lm_heads  = hparams.n_lm_heads;
        const int n_wtes      = hparams.n_wtes;

        ctx_size += n_embd*ggml_type_sizef(GGML_TYPE_F32); // ln_f_g
        ctx_size += n_embd*ggml_type_sizef(GGML_TYPE_F32); // ln_f_b

        ctx_size += n_wtes*n_in_vocab*n_embd*ggml_type_sizef(wtype);     // wte
        ctx_size += block_size*n_embd*ggml_type_sizef(GGML_TYPE_F32); // wpe
        ctx_size += n_lm_heads*n_out_vocab*n_embd*ggml_type_sizef(wtype); // lm_head

        ctx_size += n_layer*(n_embd*ggml_type_sizef(GGML_TYPE_F32)); // ln_1_g
        ctx_size += n_layer*(n_embd*ggml_type_sizef(GGML_TYPE_F32)); // ln_1_b

        ctx_size += n_layer*(n_embd*ggml_type_sizef(GGML_TYPE_F32)); // ln_2_g
        ctx_size += n_layer*(n_embd*ggml_type_sizef(GGML_TYPE_F32)); // ln_2_b

        ctx_size += n_layer*(3*n_embd*n_embd*ggml_type_sizef(wtype));         // c_attn_attn_w
        ctx_size += n_layer*(       3*n_embd*ggml_type_sizef(GGML_TYPE_F32)); // c_attn_attn_b

        ctx_size += n_layer*(n_embd*n_embd*ggml_type_sizef(wtype));           // c_attn_proj_w
        ctx_size += n_layer*(       n_embd*ggml_type_sizef(GGML_TYPE_F32));   // c_attn_proj_b

        ctx_size += n_layer*(4*n_embd*n_embd*ggml_type_sizef(wtype));         // c_mlp_fc_w
        ctx_size += n_layer*(       4*n_embd*ggml_type_sizef(GGML_TYPE_F32)); // c_mlp_fc_b

        ctx_size += n_layer*(4*n_embd*n_embd*ggml_type_sizef(wtype));         // c_mlp_proj_w
        ctx_size += n_layer*(         n_embd*ggml_type_sizef(GGML_TYPE_F32)); // c_mlp_proj_b

        ctx_size += block_size*n_layer*n_embd*ggml_type_sizef(GGML_TYPE_F32); // memory_k
        ctx_size += block_size*n_layer*n_embd*ggml_type_sizef(GGML_TYPE_F32); // memory_v

        ctx_size += (6 + 12*n_layer)*512; // object overhead

        printf("%s: ggml tensor size = %d bytes\n", __func__, (int) sizeof(ggml_tensor));
        printf("%s: ggml ctx size = %6.2f MB\n", __func__, ctx_size/(1024.0*1024.0));
    }

    // create the ggml context
    {
        struct ggml_init_params params = {
            /*.mem_size   =*/ ctx_size,
            /*.mem_buffer =*/ NULL,
            /*.no_alloc   =*/ false,
        };

        model.ctx = ggml_init(params);
        if (!model.ctx) {
            fprintf(stderr, "%s: ggml_init() failed\n", __func__);
            return false;
        }
    }

    // prepare memory for the weights
    {
        const auto & hparams = model.hparams;

        const int n_embd      = hparams.n_embd;
        const int n_layer     = hparams.n_layer;
        const int block_size  = hparams.block_size;
        const int n_in_vocab  = hparams.n_in_vocab;
        const int n_out_vocab = hparams.n_out_vocab;
        const int n_lm_heads  = hparams.n_lm_heads;
        const int n_wtes      = hparams.n_wtes;

        model.layers.resize(n_layer);
        model.lm_heads.resize(n_lm_heads);
        model.wtes.resize(n_wtes);

        model.ln_f_g = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, n_embd);
        model.ln_f_b = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, n_embd);

        model.wpe = ggml_new_tensor_2d(ctx, GGML_TYPE_F32, n_embd, block_size);

        for (int i = 0; i < n_wtes; i++) {
            model.wtes[i] = ggml_new_tensor_2d(ctx, wtype, n_embd, n_in_vocab);
            model.tensors["model/wte/" + std::to_string(i)] = model.wtes[i];
        }

        for (int i = 0; i < n_lm_heads; i++) {
            model.lm_heads[i] = ggml_new_tensor_2d(ctx, wtype, n_embd, n_out_vocab);
            model.tensors["model/lm_head/" + std::to_string(i)] = model.lm_heads[i];
        }

        model.tensors["model/ln_f/g"] = model.ln_f_g;
        model.tensors["model/ln_f/b"] = model.ln_f_b;

        model.tensors["model/wpe"]     = model.wpe;

        for (int i = 0; i < n_layer; ++i) {
            auto & layer = model.layers[i];

            layer.ln_1_g        = ggml_new_tensor_1d(ctx, GGML_TYPE_F32,   n_embd);
            layer.ln_1_b        = ggml_new_tensor_1d(ctx, GGML_TYPE_F32,   n_embd);

            layer.ln_2_g        = ggml_new_tensor_1d(ctx, GGML_TYPE_F32,   n_embd);
            layer.ln_2_b        = ggml_new_tensor_1d(ctx, GGML_TYPE_F32,   n_embd);

            layer.c_attn_attn_w = ggml_new_tensor_2d(ctx, wtype,           n_embd, 3*n_embd);
            layer.c_attn_attn_b = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, 3*n_embd);

            layer.c_attn_proj_w = ggml_new_tensor_2d(ctx, wtype,           n_embd, n_embd);
            layer.c_attn_proj_b = ggml_new_tensor_1d(ctx, GGML_TYPE_F32,   n_embd);

            layer.c_mlp_fc_w    = ggml_new_tensor_2d(ctx, wtype,           n_embd, 4*n_embd);
            layer.c_mlp_fc_b    = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, 4*n_embd);

            layer.c_mlp_proj_w  = ggml_new_tensor_2d(ctx, wtype,         4*n_embd, n_embd);
            layer.c_mlp_proj_b  = ggml_new_tensor_1d(ctx, GGML_TYPE_F32,   n_embd);

            // map by name
            model.tensors["model/h" + std::to_string(i) + "/ln_1/g"]        = layer.ln_1_g;
            model.tensors["model/h" + std::to_string(i) + "/ln_1/b"]        = layer.ln_1_b;

            model.tensors["model/h" + std::to_string(i) + "/ln_2/g"]        = layer.ln_2_g;
            model.tensors["model/h" + std::to_string(i) + "/ln_2/b"]        = layer.ln_2_b;

            model.tensors["model/h" + std::to_string(i) + "/attn/c_attn/w"] = layer.c_attn_attn_w;
            model.tensors["model/h" + std::to_string(i) + "/attn/c_attn/b"] = layer.c_attn_attn_b;

            model.tensors["model/h" + std::to_string(i) + "/attn/c_proj/w"] = layer.c_attn_proj_w;
            model.tensors["model/h" + std::to_string(i) + "/attn/c_proj/b"] = layer.c_attn_proj_b;

            model.tensors["model/h" + std::to_string(i) + "/mlp/c_fc/w"]    = layer.c_mlp_fc_w;
            model.tensors["model/h" + std::to_string(i) + "/mlp/c_fc/b"]    = layer.c_mlp_fc_b;

            model.tensors["model/h" + std::to_string(i) + "/mlp/c_proj/w"]  = layer.c_mlp_proj_w;
            model.tensors["model/h" + std::to_string(i) + "/mlp/c_proj/b"]  = layer.c_mlp_proj_b;
        }
    }

    // key + value memory
    {
        const auto & hparams = model.hparams;

        const int n_embd     = hparams.n_embd;
        const int n_layer    = hparams.n_layer;
        const int block_size = hparams.block_size;

        const int n_mem      = n_layer*block_size;
        const int n_elements = n_embd*n_mem;

        model.memory_k = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, n_elements);
        model.memory_v = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, n_elements);

        const size_t memory_size = ggml_nbytes(model.memory_k) + ggml_nbytes(model.memory_v);

        printf("%s: memory size = %8.2f MB, n_mem = %d\n", __func__, memory_size/1024.0/1024.0, n_mem);
    }

    // load weights
    {
        size_t total_size = 0;

        while(true) {
            int32_t n_dims;
            int32_t length;
            int32_t ttype;

            read_safe(fin, n_dims);
            read_safe(fin, length);
            read_safe(fin, ttype);

            if (fin.eof()) {
                break;
            }

            int32_t nelements = 1;
            int32_t ne[2] = { 1, 1 };
            for (int i = 0; i < n_dims; ++i) {
                read_safe(fin, ne[i]);
                nelements *= ne[i];
            }

            std::string name(length, 0);
            fin.read(&name[0], length);

            if (model.tensors.find(name.data()) == model.tensors.end()) {
                fprintf(stderr, "%s: unknown tensor '%s' in model file\n", __func__, name.data());
                return false;
            }

            auto tensor = model.tensors[name.data()];
            if (ggml_nelements(tensor) != nelements) {
                fprintf(stderr, "%s: tensor '%s' has wrong size in model file\n", __func__, name.data());
                return false;
            }

            if (tensor->ne[0] != ne[0] || tensor->ne[1] != ne[1]) {
                fprintf(stderr, "%s: tensor '%s' has wrong shape in model file: got [%d, %d], expected [%d, %d]\n",
                        __func__, name.data(), (int) tensor->ne[0], (int) tensor->ne[1], ne[0], ne[1]);
                return false;
            }

            const size_t bpe = ggml_type_size(ggml_type(ttype));

            if ((nelements*bpe)/ggml_blck_size(tensor->type) != ggml_nbytes(tensor)) {
                fprintf(stderr, "%s: tensor '%s' has wrong size in model file: got %zu, expected %zu\n",
                        __func__, name.data(), ggml_nbytes(tensor), nelements*bpe);
                return false;
            }

            fin.read(reinterpret_cast<char *>(tensor->data), ggml_nbytes(tensor));

            // printf("%48s - [%5d, %5d], type = %6s, %6.2f MB\n", name.data(), ne[0], ne[1], "float", ggml_nbytes(tensor)/1024.0/1024.0);

            total_size += ggml_nbytes(tensor);
        }

        printf("%s: model size  = %8.2f MB\n", __func__, total_size/1024.0/1024.0);
        model.memsize = total_size;
    }

    fin.close();

    return true;
}

bool bark_model_load(const std::string & dirname, bark_model & model) {
    printf("%s: loading model from '%s'\n", __func__, dirname.c_str());

    // text
    {
        printf("%s: reading bark text model\n", __func__);
        const std::string fname = dirname + "/ggml_weights_text.bin";
        if(!gpt_model_load(fname, model.text_model)) {
            fprintf(stderr, "%s: invalid model file '%s' (bad text)\n", __func__, fname.c_str());
            return false;
        }
        model.memsize += model.text_model.memsize;
    }

    // vocab
    {
        printf("%s: reading bark vocab\n", __func__);
        const std::string fname     = dirname + "/ggml_vocab.bin";
        const gpt_hparams hparams   = model.text_model.hparams;
        const int32_t expected_size = hparams.n_in_vocab - hparams.n_out_vocab - 5;
        if(!bark_vocab_load(fname, model.vocab, expected_size)) {
            fprintf(stderr, "%s: invalid model file '%s' (bad text)\n", __func__, fname.c_str());
            return false;
        }
    }

    // coarse
    {
        printf("\n%s: reading bark coarse model\n", __func__);
        const std::string fname = dirname + "/ggml_weights_coarse.bin";
        if(!gpt_model_load(fname, model.coarse_model)) {
            fprintf(stderr, "%s: invalid model file '%s' (bad coarse)\n", __func__, fname.c_str());
            return false;
        }
        model.memsize += model.coarse_model.memsize;
    }

    // fine
    {
        printf("\n%s: reading bark fine model\n", __func__);
        const std::string fname = dirname + "/ggml_weights_fine.bin";
        if(!gpt_model_load(fname, model.fine_model)) {
            fprintf(stderr, "%s: invalid model file '%s' (bad fine)\n", __func__, fname.c_str());
            return false;
        }
        model.memsize += model.fine_model.memsize;
    }

    // codec
    {
        printf("\n%s: reading bark codec model\n", __func__);
        const std::string fname = dirname + "/ggml_weights_codec.bin";
        if(!encodec_model_load(fname, model.codec_model)) {
            fprintf(stderr, "%s: invalid model file '%s' (bad codec)\n", __func__, fname.c_str());
            return false;
        }
        model.memsize += model.codec_model.memsize;
    }

    printf("\n%s: total model size  = %8.2f MB\n", __func__, model.memsize/1024.0/1024.0);

    return true;
}

std::string strip_accents(const std::string &in_str) {
    std::string out_str;
    std::map<std::string, char> accent_map = {{"À", 'A'},{"Á", 'A'},
        {"Â", 'A'},{"Ã", 'A'},{"Ä", 'A'},{"Å", 'A'},{"à", 'a'},{"á", 'a'},
        {"â", 'a'},{"ã", 'a'},{"ä", 'a'},{"å", 'a'},{"È", 'E'},{"É", 'E'},
        {"Ê", 'E'},{"Ë", 'E'},{"è", 'e'},{"é", 'e'},{"ê", 'e'},{"ë", 'e'},
        {"Ì", 'I'},{"Í", 'I'},{"Î", 'I'},{"Ï", 'I'},{"ì", 'i'},{"í", 'i'},
        {"î", 'i'},{"ï", 'i'},{"Ò", 'O'},{"Ó", 'O'},{"Ô", 'O'},{"Õ", 'O'},
        {"Ö", 'O'},{"ò", 'o'},{"ó", 'o'},{"ô", 'o'},{"õ", 'o'},{"ö", 'o'},
        {"Ù", 'U'},{"Ú", 'U'},{"Û", 'U'},{"Ü", 'U'},{"ù", 'u'},{"ú", 'u'},
        {"û", 'u'},{"ü", 'u'},{"Ý", 'Y'},{"ý", 'y'},{"Ç", 'C'},{"ç", 'c'},
        {"Ñ", 'N'},{"ñ", 'n'},
    };

    for (size_t i = 0; i < in_str.length();) {
        int len = utf8_len(in_str[i]);
        std::string cur = in_str.substr(i, len);
        auto iter = accent_map.find(cur);
        if (iter != accent_map.end())
            out_str += iter->second;
        else
            out_str += cur;

        i += len;
    }

    return out_str;
}

void bert_tokenize(
        const bark_vocab & vocab,
              const char * text,
                 int32_t * tokens,
                 int32_t * n_tokens,
                 int32_t   n_max_tokens) {
    std::string str = text;
    std::vector<std::string> words;

    int32_t t = 0;

    auto * token_map = &vocab.token_to_id;

    // split the text into words
    {
        str = strip_accents(text);

        std::string pat = R"([[:punct:]]|[[:alpha:]]+|[[:digit:]]+)";

        std::regex re(pat);
        std::smatch m;

        while (std::regex_search(str, m, re)) {
            for (std::string x : m)
                words.push_back(x);
            str = m.suffix();
        }
    }

    // apply wordpiece
    for (const auto &word : words) {
        if (word.size() == 0)
            continue;

        std::string prefix = "";
        int i = 0;
        int n = word.size();

        loop:
            while (i < n) {
                if (t >= n_max_tokens - 1)
                    break;
                int j = n;
                while (j > i) {
                    auto it = token_map->find(prefix + word.substr(i, j - i));
                    if (it != token_map->end()) {
                        tokens[t++] = it->second;
                        i = j;
                        prefix = "##";
                        goto loop;
                    }
                    --j;
                }
                if (j == i) {
                    fprintf(stderr, "%s: unknown token '%s'\n", __func__, word.substr(i, 1).data());
                    prefix = "##";
                    ++i;
                }
            }
        }

    *n_tokens = t;
}

bool fine_gpt_eval(
        const gpt_model & model,
        const int n_threads,
        const int codebook_ix,
        const bark_codes & embd_inp,
              std::vector<std::vector<float>> & logits,
              size_t                          & mem_per_token) {
    // embd_inp: [n_channels, seq_length]
    const int N  = embd_inp[0].size();
    const int n_codes = embd_inp.size();

    const auto & hparams = model.hparams;

    const int n_embd  = hparams.n_embd;
    const int n_layer = hparams.n_layer;
    const int n_ctx   = hparams.block_size;
    const int n_head  = hparams.n_head;
    const int n_vocab = hparams.n_out_vocab;

    const int n_codes_given = hparams.n_codes_given;

    BARK_ASSERT(N <= n_ctx);
    BARK_ASSERT(codebook_ix > 0);

    static size_t buf_size = 256u*1024*1024;
    static void * buf = malloc(buf_size);

    if (mem_per_token > 0 && mem_per_token*N*n_codes > buf_size) {
        const size_t buf_size_new = 1.2*(mem_per_token*N*n_codes); // add 20% to account for ggml object overhead

        // reallocate
        buf_size = buf_size_new;
        buf = realloc(buf, buf_size);
        if (buf == nullptr) {
            fprintf(stderr, "%s: failed to allocate %zu bytes\n", __func__, buf_size);
            return false;
        }
    }

    struct ggml_init_params params = {
        /*.mem_size   =*/ buf_size,
        /*.mem_buffer =*/ buf,
        /*.no_alloc   =*/ false,
    };

    struct ggml_context * ctx0 = ggml_init(params);
    struct ggml_cgraph gf = {};

    struct ggml_tensor * input = ggml_new_tensor_2d(ctx0, GGML_TYPE_I32, N, n_codes);
    for (int c = 0; c < n_codes; c++) {
        int offset = c*input->nb[1];
        memcpy((void *) ((char *) input->data + offset), embd_inp[c].data(), N*ggml_element_size(input));
    }

    struct ggml_tensor * tok_emb = ggml_new_tensor_2d(ctx0, GGML_TYPE_F32, n_embd, N);
    tok_emb = ggml_set_zero(tok_emb);

    for (int wte_ix = 0; wte_ix < codebook_ix + 1; wte_ix++) {
        struct ggml_tensor * cur = ggml_get_rows(ctx0,
                        model.wtes[wte_ix],
                        ggml_view_1d(ctx0, input, N, wte_ix*input->nb[1]));
        tok_emb = ggml_add(ctx0, tok_emb, cur);
    }

    struct ggml_tensor * position = ggml_new_tensor_1d(ctx0, GGML_TYPE_I32, N);
    for (int i = 0; i < N; ++i) {
        ((int32_t *) position->data)[i] = i;
    }
    struct ggml_tensor * pos_emb = ggml_get_rows(ctx0, model.wpe, position);

    // wte + wpe
    struct ggml_tensor * inpL = ggml_add(ctx0, tok_emb, pos_emb);

    for (int il = 0; il < n_layer; il++) {

        // norm
        struct ggml_tensor * cur = ggml_norm(ctx0, inpL);

        // cur = ln_1_g*cur + ln_1_b
        cur = ggml_add(ctx0,
                ggml_mul(ctx0,
                    ggml_repeat(ctx0, model.layers[il].ln_1_g, cur),
                    cur),
                ggml_repeat(ctx0, model.layers[il].ln_1_b, cur));

        // cur = attn_w*cur
        cur = ggml_mul_mat(ctx0, model.layers[il].c_attn_attn_w, cur);

        struct ggml_tensor * Qcur = ggml_view_2d(ctx0, cur, n_embd, N, cur->nb[1], 0*sizeof(float)*n_embd);
        struct ggml_tensor * Kcur = ggml_view_2d(ctx0, cur, n_embd, N, cur->nb[1], 1*sizeof(float)*n_embd);
        struct ggml_tensor * Vcur = ggml_view_2d(ctx0, cur, n_embd, N, cur->nb[1], 2*sizeof(float)*n_embd);

        // [n_embd/n_head, N, n_head]
        struct ggml_tensor * Q =
            ggml_permute(ctx0,
                    ggml_cpy(ctx0,
                        Qcur,
                        ggml_new_tensor_3d(ctx0, GGML_TYPE_F32, n_embd/n_head, n_head, N)),
                    0, 2, 1, 3);

        // [n_embd/n_head, N, n_head]
        struct ggml_tensor * K =
            ggml_permute(ctx0,
                    ggml_cpy(ctx0,
                        Kcur,
                        ggml_new_tensor_3d(ctx0, GGML_TYPE_F32, n_embd/n_head, n_head, N)),
                    0, 2, 1, 3);

        // [N, N, n_head]
        struct ggml_tensor * KQ = ggml_mul_mat(ctx0, K, Q);

        // [N, N, n_head]
        struct ggml_tensor * KQ_scaled =
            ggml_scale_inplace(ctx0,
                    KQ,
                    ggml_new_f32(ctx0, 1.0f/sqrt(float(n_embd)/n_head)));

        // [N, N, n_head]
        struct ggml_tensor * KQ_soft_max = ggml_soft_max_inplace(ctx0, KQ_scaled);

        // [N, n_embd/n_head, n_head]
        struct ggml_tensor * V_trans =
            ggml_cont(ctx0,
                ggml_permute(ctx0,
                        ggml_cpy(ctx0,
                            Vcur,
                            ggml_new_tensor_3d(ctx0, GGML_TYPE_F32, n_embd/n_head, n_head, N)),
                        1, 2, 0, 3));

        // [n_embd/n_head, N, n_head]
        struct ggml_tensor * KQV = ggml_mul_mat(ctx0, V_trans, KQ_soft_max);

        struct ggml_tensor * KQV_merged = ggml_permute(ctx0, KQV, 0, 2, 1, 3);

        cur = ggml_cpy(ctx0,
                KQV_merged,
                ggml_new_tensor_2d(ctx0, GGML_TYPE_F32, n_embd, N));

        // cur = proj_w*cur
        cur = ggml_mul_mat(ctx0,
                model.layers[il].c_attn_proj_w,
                cur);

        // residual connection
        cur = ggml_add(ctx0, cur, inpL);

        struct ggml_tensor * inpFF = cur;

        // norm
        cur = ggml_norm(ctx0, inpFF);

        // cur = ln_2_g*cur + ln_2_b
        cur = ggml_add(ctx0,
                ggml_mul(ctx0,
                    ggml_repeat(ctx0, model.layers[il].ln_2_g, cur),
                    cur),
                ggml_repeat(ctx0, model.layers[il].ln_2_b, cur));

        // cur = fc_w*cur
        cur = ggml_mul_mat(ctx0, model.layers[il].c_mlp_fc_w, cur);

        // GELU activation
        cur = ggml_gelu(ctx0, cur);

        // cur = proj_w*cur
        cur = ggml_mul_mat(ctx0, model.layers[il].c_mlp_proj_w, cur);

        // input for next layer
        inpL = ggml_add(ctx0, cur, inpFF);
    }

    // norm
    inpL = ggml_norm(ctx0, inpL);

    // inpL = ln_f_g*inpL + ln_f_b
    inpL = ggml_add(ctx0,
            ggml_mul(ctx0,
                ggml_repeat(ctx0, model.ln_f_g, inpL),
                inpL),
            ggml_repeat(ctx0, model.ln_f_b, inpL));

    // inpL = WTE * inpL
    inpL = ggml_mul_mat(ctx0, model.lm_heads[codebook_ix - n_codes_given], inpL);

    // run the computation
    ggml_build_forward_expand(&gf, inpL);
    ggml_graph_compute_with_ctx(ctx0, &gf, n_threads);

    if (mem_per_token > 0) {
        // struct ggml_tensor * gt_kqv = ggml_new_tensor_2d(ctx0, GGML_TYPE_F32, n_embd, N);
        // load_gt_tensor("./data/debug_kqv.bin", gt_kqv);

        // struct ggml_tensor * gt_inpL = ggml_new_tensor_2d(ctx0, GGML_TYPE_F32, n_embd, N);
        // load_gt_tensor("./data/test_out_block_1.bin", gt_inpL);

        // struct ggml_tensor * gt_inpFF = ggml_new_tensor_2d(ctx0, GGML_TYPE_F32, n_embd, N);
        // load_gt_tensor("./data/debug_inpFF.bin", gt_inpFF);

        // struct ggml_tensor * gt_outLN2 = ggml_new_tensor_2d(ctx0, GGML_TYPE_F32, n_embd, N);
        // load_gt_tensor("./data/debug_ln2.bin", gt_outLN2);

        // struct ggml_tensor * gt_outPROJ1 = ggml_new_tensor_2d(ctx0, GGML_TYPE_F32, 4*n_embd, N);
        // load_gt_tensor("./data/debug_outPROJ1.bin", gt_outPROJ1);

        // struct ggml_tensor * gt_outGELU = ggml_new_tensor_2d(ctx0, GGML_TYPE_F32, 4*n_embd, N);
        // load_gt_tensor("./data/debug_outGELU.bin", gt_outGELU);

        // struct ggml_tensor * gt_outPROJ2 = ggml_new_tensor_2d(ctx0, GGML_TYPE_F32, n_embd, N);
        // load_gt_tensor("./data/debug_outPROJ2.bin", gt_outPROJ2);

        // struct ggml_tensor * gt_PROJ2w = ggml_new_tensor_2d(ctx0, GGML_TYPE_F32, 4*n_embd, n_embd);
        // load_gt_tensor("./data/debug_weight_PROJ2.bin", gt_PROJ2w);

        // printf("\n");

        // float tol = 0.001;

        // allclose(gt_kqv     , kqv_toy , tol, "KQV"     );
        // allclose(gt_inpFF   , inpFF   , tol, "inpFF"   );
        // allclose(gt_outLN2  , outLN2  , tol, "outLN2"  );
        // allclose(gt_outPROJ1, outPROJ1, tol, "outPROJ1");
        // allclose(gt_outGELU , outGELU , tol, "outGELU" );
        // allclose(gt_outPROJ2, outPROJ2, tol, "outPROJ2");
        // allclose(gt_PROJ2w  , model.layers[0].c_mlp_proj_w, tol, "PROJ2_w");
    }

    // [N, n_vocab]
    // [1024, 1056]
    logits.resize(N);

    for (int i = 0; i < N; i++) {
        std::vector<float> tmp(n_vocab);
        memcpy(tmp.data(), (float *) ggml_get_data(inpL) + (i*n_vocab), sizeof(float)*n_vocab);
        logits[i] = tmp;
    }

    if (mem_per_token == 0) {
        mem_per_token = ggml_used_mem(ctx0)/N;
    }

    ggml_free(ctx0);

    return true;
}


bool gpt_eval(
        const gpt_model & model,
        const int n_threads,
        int * n_past,
        const bool merge_ctx,
        const bark_sequence & embd_inp,
              std::vector<float>          & embd_w,
              size_t                      & mem_per_token) {
    int N = embd_inp.size();
    BARK_ASSERT(n_past != NULL);

    const auto & hparams = model.hparams;

    const int n_embd  = hparams.n_embd;
    const int n_layer = hparams.n_layer;
    const int n_ctx   = hparams.block_size;
    const int n_head  = hparams.n_head;
    const int n_vocab = hparams.n_out_vocab;

    static size_t buf_size = 256u*1024*1024;
    static void * buf = malloc(buf_size);

    if (mem_per_token > 0 && mem_per_token*N > buf_size) {
        const size_t buf_size_new = 1.2*(mem_per_token*N); // add 20% to account for ggml object overhead

        // reallocate
        buf_size = buf_size_new;
        buf = realloc(buf, buf_size);
        if (buf == nullptr) {
            fprintf(stderr, "%s: failed to allocate %zu bytes\n", __func__, buf_size);
            return false;
        }
    }

    struct ggml_init_params params = {
        /*.mem_size   =*/ buf_size,
        /*.mem_buffer =*/ buf,
        /*.no_alloc   =*/ false,
    };

    struct ggml_context * ctx0 = ggml_init(params);
    struct ggml_cgraph gf = {};

    struct ggml_tensor * input = ggml_new_tensor_1d(ctx0, GGML_TYPE_I32, N);
    memcpy(input->data, embd_inp.data(), N*ggml_element_size(input));

    struct ggml_tensor * tok_emb;

    if (*n_past > 0) {
        BARK_ASSERT(N == 1);
        tok_emb = ggml_get_rows(ctx0, model.wtes[0], input);
    } else {
        if (merge_ctx) {
            BARK_ASSERT(N == 256+256+1);
            N -= 256;
        } else {
            BARK_ASSERT(N <= n_ctx);
        }

        if (merge_ctx) {
            struct ggml_tensor * seq_embd = ggml_get_rows(ctx0, model.wtes[0], ggml_view_1d(ctx0, input, 256, 0));
            struct ggml_tensor * ctx_embd = ggml_get_rows(ctx0, model.wtes[0], ggml_view_1d(ctx0, input, 256, 256*ggml_element_size(input)));
            struct ggml_tensor * rem_embd = ggml_get_rows(ctx0, model.wtes[0], ggml_view_1d(ctx0, input,   1, 512*ggml_element_size(input)));

            struct ggml_tensor * cat_emb = ggml_add(ctx0, seq_embd, ctx_embd);

            tok_emb = ggml_new_tensor_2d(ctx0, cat_emb->type, cat_emb->ne[0], cat_emb->ne[1]+rem_embd->ne[1]);
            tok_emb = ggml_set_1d(ctx0, tok_emb, cat_emb, 0);
            tok_emb = ggml_set_1d(ctx0, tok_emb, rem_embd, cat_emb->ne[0]*cat_emb->ne[1]*ggml_element_size(cat_emb));
        } else {
            tok_emb = ggml_get_rows(ctx0, model.wtes[0], input);
        }
    }

    struct ggml_tensor * position = ggml_new_tensor_1d(ctx0, GGML_TYPE_I32, N);
    for (int i = 0; i < N; ++i) {
        ((int32_t *) position->data)[i] = *n_past + i;
    }

    // wte + wpe
    struct ggml_tensor * inpL = ggml_add(ctx0, tok_emb, ggml_get_rows(ctx0, model.wpe, position));

    for (int il = 0; il < n_layer; ++il) {
        struct ggml_tensor * cur;

        // norm
        {
            // [ 768, N]
            cur = ggml_norm(ctx0, inpL);

            // cur = ln_1_g*cur + ln_1_b
            // [ 768, N]
            cur = ggml_add(ctx0,
                    ggml_mul(ctx0,
                        ggml_repeat(ctx0, model.layers[il].ln_1_g, cur),
                        cur),
                    ggml_repeat(ctx0, model.layers[il].ln_1_b, cur));
        }

        // attn
        // [2304, 768] - model.layers[il].c_attn_attn_w
        // [2304,   1] - model.layers[il].c_attn_attn_b
        // [ 768,   N] - cur (in)
        // [2304,   N] - cur (out)
        //
        // cur = attn_w*cur + attn_b
        // [2304, N]
        {
            cur = ggml_mul_mat(ctx0,
                    model.layers[il].c_attn_attn_w,
                    cur);

            cur = ggml_add(ctx0,
                    ggml_repeat(ctx0, model.layers[il].c_attn_attn_b, cur),
                    cur);
        }

        // self-attention
        {
            struct ggml_tensor * Qcur = ggml_view_2d(ctx0, cur, n_embd, N, cur->nb[1], 0*sizeof(float)*n_embd);
            struct ggml_tensor * Kcur = ggml_view_2d(ctx0, cur, n_embd, N, cur->nb[1], 1*sizeof(float)*n_embd);
            struct ggml_tensor * Vcur = ggml_view_2d(ctx0, cur, n_embd, N, cur->nb[1], 2*sizeof(float)*n_embd);

            // store key and value to memory
            if (N >= 1) {
                struct ggml_tensor * k = ggml_view_1d(ctx0, model.memory_k, N*n_embd, (ggml_element_size(model.memory_k)*n_embd)*(il*n_ctx + *n_past));
                struct ggml_tensor * v = ggml_view_1d(ctx0, model.memory_v, N*n_embd, (ggml_element_size(model.memory_v)*n_embd)*(il*n_ctx + *n_past));

                ggml_build_forward_expand(&gf, ggml_cpy(ctx0, Kcur, k));
                ggml_build_forward_expand(&gf, ggml_cpy(ctx0, Vcur, v));
            }

            // Q = Qcur.contiguous().view(n_embd/n_head, n_head, N).permute(0, 2, 1, 3)
            // [64, N, 12]
            struct ggml_tensor * Q =
                ggml_permute(ctx0,
                        ggml_cpy(ctx0,
                            Qcur,
                            ggml_new_tensor_3d(ctx0, GGML_TYPE_F32, n_embd/n_head, n_head, N)),
                        0, 2, 1, 3);

            // K = Kmem.view(n_embd/n_head, n_head, n_past + N).permute(0, 2, 1, 3)
            // [64, n_past + N, 12]
            struct ggml_tensor * K =
                ggml_permute(ctx0,
                        ggml_reshape_3d(ctx0,
                            ggml_view_1d(ctx0, model.memory_k, (*n_past + N)*n_embd, il*n_ctx*ggml_element_size(model.memory_k)*n_embd),
                            n_embd/n_head, n_head, *n_past + N),
                        0, 2, 1, 3);

            // GG: flash attention
            //struct ggml_tensor * V =
            //    ggml_cpy(ctx0,
            //            ggml_permute(ctx0,
            //                ggml_reshape_3d(ctx0,
            //                    ggml_view_1d(ctx0, model.memory_v, (n_past + N)*n_embd, il*n_ctx*ggml_element_size(model.memory_v)*n_embd),
            //                    n_embd/n_head, n_head, n_past + N),
            //                1, 2, 0, 3),
            //            ggml_new_tensor_3d(ctx0, GGML_TYPE_F32, n_past + N, n_embd/n_head, n_head));

            //struct ggml_tensor * KQV = ggml_flash_attn(ctx0, Q, K, V, true);

            // K * Q
            // [n_past + N, N, 12]
            struct ggml_tensor * KQ = ggml_mul_mat(ctx0, K, Q);

            // KQ_scaled = KQ / sqrt(n_embd/n_head)
            // [n_past + N, N, 12]
            struct ggml_tensor * KQ_scaled =
                ggml_scale_inplace(ctx0,
                        KQ,
                        ggml_new_f32(ctx0, 1.0f/sqrt(float(n_embd)/n_head))
                        );

            // KQ_masked = mask_past(KQ_scaled)
            // [n_past + N, N, 12]
            struct ggml_tensor * KQ_masked = ggml_diag_mask_inf_inplace(ctx0, KQ_scaled, *n_past);

            // KQ = soft_max(KQ_masked)
            // [n_past + N, N, 12]
            struct ggml_tensor * KQ_soft_max = ggml_soft_max_inplace(ctx0, KQ_masked);

            // V_trans = Vmem.view(n_embd/n_head, n_head, n_past + N).permute(1, 2, 0, 3).contiguous()
            // [n_past + N, 64, 12]
            struct ggml_tensor * V_trans =
                ggml_cpy(ctx0,
                        ggml_permute(ctx0,
                            ggml_reshape_3d(ctx0,
                                ggml_view_1d(ctx0, model.memory_v, (*n_past + N)*n_embd, il*n_ctx*ggml_element_size(model.memory_v)*n_embd),
                                n_embd/n_head, n_head, *n_past + N),
                            1, 2, 0, 3),
                        ggml_new_tensor_3d(ctx0, model.memory_v->type, *n_past + N, n_embd/n_head, n_head));

            // KQV = transpose(V) * KQ_soft_max
            // [64, N, 12]
            struct ggml_tensor * KQV = ggml_mul_mat(ctx0, V_trans, KQ_soft_max);

            // KQV_merged = KQV.permute(0, 2, 1, 3)
            // [64, 12, N]
            struct ggml_tensor * KQV_merged = ggml_permute(ctx0, KQV, 0, 2, 1, 3);

            // cur = KQV_merged.contiguous().view(n_embd, N)
            // [768, N]
            cur = ggml_cpy(ctx0,
                    KQV_merged,
                    ggml_new_tensor_2d(ctx0, GGML_TYPE_F32, n_embd, N));
        }

        // projection
        // [ 768, 768] - model.layers[il].c_attn_proj_w
        // [ 768,   1] - model.layers[il].c_attn_proj_b
        // [ 768,   N] - cur (in)
        // [ 768,   N] - cur (out)
        //
        // cur = proj_w*cur + proj_b
        // [768, N]
        {
            cur = ggml_mul_mat(ctx0,
                    model.layers[il].c_attn_proj_w,
                    cur);

            cur = ggml_add(ctx0,
                    ggml_repeat(ctx0, model.layers[il].c_attn_proj_b, cur),
                    cur);
        }

        // add the input
        cur = ggml_add(ctx0, cur, inpL);

        struct ggml_tensor * inpFF = cur;

        // feed-forward network
        {
            // norm
            {
                cur = ggml_norm(ctx0, inpFF);

                // cur = ln_2_g*cur + ln_2_b
                // [ 768, N]
                cur = ggml_add(ctx0,
                        ggml_mul(ctx0,
                            ggml_repeat(ctx0, model.layers[il].ln_2_g, cur),
                            cur),
                        ggml_repeat(ctx0, model.layers[il].ln_2_b, cur));
            }

            // fully connected
            // [3072, 768] - model.layers[il].c_mlp_fc_w
            // [3072,   1] - model.layers[il].c_mlp_fc_b
            // [ 768,   N] - cur (in)
            // [3072,   N] - cur (out)
            //
            // cur = fc_w*cur + fc_b
            // [3072, N]
            cur = ggml_mul_mat(ctx0,
                    model.layers[il].c_mlp_fc_w,
                    cur);

            cur = ggml_add(ctx0,
                    ggml_repeat(ctx0, model.layers[il].c_mlp_fc_b, cur),
                    cur);
            
            // GELU activation
            // [3072, N]
            cur = ggml_gelu(ctx0, cur);

            // projection
            // [ 768, 3072] - model.layers[il].c_mlp_proj_w
            // [ 768,    1] - model.layers[il].c_mlp_proj_b
            // [3072,    N] - cur (in)
            // [ 768,    N] - cur (out)
            //
            // cur = proj_w*cur + proj_b
            // [768, N]
            cur = ggml_mul_mat(ctx0,
                    model.layers[il].c_mlp_proj_w,
                    cur);

            cur = ggml_add(ctx0,
                    ggml_repeat(ctx0, model.layers[il].c_mlp_proj_b, cur),
                    cur);
        }

        // input for next layer
        inpL = ggml_add(ctx0, cur, inpFF);
    }

    // norm
    {
        // [ 768, N]
        inpL = ggml_norm(ctx0, inpL);

        // inpL = ln_f_g*inpL + ln_f_b
        // [ 768, N]
        inpL = ggml_add(ctx0,
                ggml_mul(ctx0,
                    ggml_repeat(ctx0, model.ln_f_g, inpL),
                    inpL),
                ggml_repeat(ctx0, model.ln_f_b, inpL));
    }

    // inpL = WTE * inpL
    // [ 768, 50257] - model.lm_head
    // [ 768, N]     - inpL
    inpL = ggml_mul_mat(ctx0, model.lm_heads[0], inpL);

    // run the computation
    ggml_build_forward_expand(&gf, inpL);
    ggml_graph_compute_with_ctx(ctx0, &gf, n_threads);

    // return result just for the last token
    embd_w.resize(n_vocab);
    memcpy(embd_w.data(), (float *) ggml_get_data(inpL) + (n_vocab*(N-1)), sizeof(float)*n_vocab);

    if (mem_per_token == 0) {
        mem_per_token = ggml_used_mem(ctx0)/N;
    }

    // updating n_past with N (-256 if merge_ctx)
    if (n_past)
        *n_past += N;

    ggml_free(ctx0);

    return true;
}

void softmax(std::vector<float> & logits) {
    // for numerical stability
    float maxl = -INFINITY;
    for (const auto & l : logits)
        maxl = std::max(maxl, l);

    // softmax
    float sum = 0.0;
    for (auto & l : logits) {
        l = exp(l - maxl);
        sum += l;
    }

    for (auto & l : logits)
        l /= sum;
}

bark_vocab::id gpt_multinomial_sample(
        std::vector<float> & logits,
        std::mt19937 & rng,
        float temp,
        float * eos_p) {
    int n_logits = logits.size();

    for (int i = 0; i < n_logits; ++i)
        logits[i] /= temp;

    softmax(logits);

    std::discrete_distribution<bark_vocab::id> dist(logits.begin(), logits.end());
    int next = dist(rng);

    // likelihood of EOS token
    if (eos_p)
        *eos_p = logits[logits.size() - 1];

    return next;
}

bark_vocab::id gpt_argmax_sample(std::vector<float> & logits, float * eos_p) {
    int n_logits = logits.size();

    // testing purposes
    for (auto & l : logits) { l /= 0.7f; }

    // likelihood of EOS token
    softmax(logits);

    if (eos_p)
        *eos_p = logits[logits.size() - 1];

    int next = 0;
    float maxl = -INFINITY;

    for (int i = 0; i < n_logits; i++) {
        if (logits[i] > maxl) {
            maxl = logits[i];
            next = i;
        }
    }

    return next;
}

bark_vocab::id gpt_sample(std::vector<float> & logits, std::mt19937 & rng, float temp, float * eos_p) {
    if (temp == 0.0f)
        return gpt_argmax_sample(logits, eos_p);
    return gpt_multinomial_sample(logits, rng, temp, eos_p);
}

bark_sequence bark_tokenize_input(const char * text, const bark_vocab & vocab, int32_t block_size) {
    int32_t max_ctx_size = std::min(block_size, 256);
    int32_t n_tokens;

    bark_sequence tokens(max_ctx_size);
    bert_tokenize(vocab, text, tokens.data(), &n_tokens, max_ctx_size);

    for (int i = 0; i < (int) tokens.size(); i++)
        tokens[i] += TEXT_ENCODING_OFFSET;

    if (n_tokens < max_ctx_size) {
        for (int i = n_tokens; i < max_ctx_size; i++)
            tokens[i] = TEXT_PAD_TOKEN;
    } else if (n_tokens > max_ctx_size) {
        fprintf(stderr, "%s: input sequence is too long (%d > 256), truncating sequence", __func__, n_tokens);
    }

    tokens.resize(max_ctx_size);

    // semantic history
    for (int i = 0; i < 256; i++)
        tokens.push_back(SEMANTIC_PAD_TOKEN);
    tokens.push_back(SEMANTIC_INFER_TOKEN);

    assert(tokens.size() == 256 + 256 + 1);

    return tokens;
}

bark_sequence bark_forward_text_encoder(
    const bark_sequence & tokens,
    const gpt_model model,
    std::mt19937 & rng,
    const int n_threads,
    const float temp,
    const float min_eos_p) {

    bark_sequence out;

    bark_progress progress;
    progress.func = __func__;

    int64_t t_sample_us  = 0;
    int64_t t_predict_us = 0;

    const int64_t t_main_start_us = ggml_time_us();

    float eos_p = 0;

    bark_sequence input = tokens;
    std::vector<float> logits;

    // dry run to estimate mem_per_token
    size_t mem_per_token = 0;
    {
        int n_past = 0;
        gpt_eval(model, n_threads, &n_past, false, { 0, 1, 2, 3 }, logits, mem_per_token);
    }

    int n_past = 0;

    for (int i = 0; i < 768; i++) {
        int64_t t_predict_start_us = ggml_time_us();
        gpt_eval(model, n_threads, &n_past, true, input, logits, mem_per_token);
        t_predict_us += (ggml_time_us() - t_predict_start_us);

        std::vector<float> relevant_logits(logits.begin(), logits.begin() + SEMANTIC_VOCAB_SIZE);
        relevant_logits.push_back(logits[SEMANTIC_PAD_TOKEN]);

        input.clear();

        int64_t t_sample_start_us = ggml_time_us();
        bark_vocab::id next = gpt_sample(logits, rng, temp, &eos_p);
        t_sample_us += (ggml_time_us() - t_sample_start_us);

        if (next == SEMANTIC_VOCAB_SIZE || eos_p >= min_eos_p)
            break;

        input.push_back(next);
        out.push_back(next);

        progress.callback((float) i/768);
    }

    const int64_t t_main_end_us = ggml_time_us();

    printf("\n\n");
    printf("%s: mem per token = %8.2f MB\n", __func__, mem_per_token/1000.0f/1000.0f);
    printf("%s:   sample time = %8.2f ms\n", __func__, t_sample_us/1000.0f);
    printf("%s:  predict time = %8.2f ms / %.2f ms per token\n", __func__, t_predict_us/1000.0f, t_predict_us/1000.0f/n_past);
    printf("%s:    total time = %8.2f ms\n", __func__, (t_main_end_us - t_main_start_us)/1000.0f);

    return out;
}

bark_codes bark_forward_coarse_encoder(
    const bark_sequence & tokens,
    const gpt_model model,
    std::mt19937 & rng,
    const int n_threads,
    const float temp,
    const int max_coarse_history,
    const int sliding_window_size) {
    bark_codes out_coarse;
    bark_sequence out;

    bark_progress progress;
    progress.func = __func__;

    int64_t t_sample_us  = 0;
    int64_t t_predict_us = 0;

    const int64_t t_main_start_us = ggml_time_us();

    float semantic_to_coarse_ratio = COARSE_RATE_HZ / SEMANTIC_RATE_HZ * N_COARSE_CODEBOOKS;
    int max_semantic_history = floorf(max_coarse_history / semantic_to_coarse_ratio);

    int n_steps = floorf(tokens.size() * semantic_to_coarse_ratio / N_COARSE_CODEBOOKS) * N_COARSE_CODEBOOKS;
    int step_ix = 0;

    BARK_ASSERT(n_steps > 0);
    BARK_ASSERT(n_steps % N_COARSE_CODEBOOKS == 0);

    int n_window_steps = ceilf(static_cast<float>(n_steps) / sliding_window_size);

    bark_sequence input = tokens;
    std::vector<float> logits;

    // dry run to estimate mem_per_token
    size_t mem_per_token = 0;
    {
        int n_past = 0;
        gpt_eval(model, n_threads, &n_past, false, { 0, 1, 2, 3 }, logits, mem_per_token);
    }

    for (int i = 0; i < n_window_steps; i++) {
        int semantic_ix = roundf(n_steps / semantic_to_coarse_ratio);

        bark_sequence input_in(
            input.begin() + std::max(semantic_ix-max_semantic_history, 0),
            input.end()
        );
        size_t original_size = input_in.size();
        input_in.resize(256);

        // padding from the right side
        for (int ix = original_size; ix < 256; ix++)
            input_in[ix] = COARSE_SEMANTIC_PAD_TOKEN;

        input_in.push_back(COARSE_INFER_TOKEN);

        // concatenate input_in and input_coarse
        input_in.insert(
            input_in.end(),
            std::make_move_iterator(out.end() - std::min(max_coarse_history, (int) out.size())),
            std::make_move_iterator(out.end())
        );

        int n_past = 0;
        mem_per_token *= 1.1;  // context length is growing, mem_per_token must grow as well

        for (int j = 0; j < sliding_window_size; j++) {
            if (step_ix >= n_steps)
                continue;

            int64_t t_predict_start_us = ggml_time_us();
            gpt_eval(model, n_threads, &n_past, false, input_in, logits, mem_per_token);
            t_predict_us += (ggml_time_us() - t_predict_start_us);

            input_in.clear();

            bool is_major = step_ix % N_COARSE_CODEBOOKS == 0;
            int start_ix  = SEMANTIC_VOCAB_SIZE + (1 - is_major) * CODEBOOK_SIZE;
            int end_ix    = SEMANTIC_VOCAB_SIZE + (2 - is_major) * CODEBOOK_SIZE;
            std::vector<float> relevant_logits(logits.begin() + start_ix, logits.begin() + end_ix);

            int64_t t_sample_start_us = ggml_time_us();
            bark_vocab::id next = gpt_sample(relevant_logits, rng, temp, NULL);
            t_sample_us += (ggml_time_us() - t_sample_start_us);

            next += start_ix;

            input_in.push_back(next);
            out.push_back(next);

            // printf("%d ", next);
            // fflush(stdout);

            step_ix += 1;

            progress.callback((float) (i*sliding_window_size+j)/n_steps);
        }
    }

    BARK_ASSERT((int) out.size() == n_steps);
    BARK_ASSERT(out.size() % N_COARSE_CODEBOOKS == 0);

    // out_coarse: [seq_length, n_codes]
    for (int i = 0; i < (int) out.size(); i += N_COARSE_CODEBOOKS) {
        // this assumes N_COARSE_CODEBOOKS = 2
        bark_sequence _tmp = {
            out[i] - SEMANTIC_VOCAB_SIZE,
            out[i+1] - SEMANTIC_VOCAB_SIZE - CODEBOOK_SIZE
        };
        out_coarse.push_back(_tmp);
    }

    const int64_t t_main_end_us = ggml_time_us();

    printf("\n\n");
    printf("%s: mem per token = %8.2f MB\n", __func__, mem_per_token/1000.0f/1000.0f);
    printf("%s:   sample time = %8.2f ms\n", __func__, t_sample_us/1000.0f);
    printf("%s:  predict time = %8.2f ms / %.2f ms per token\n", __func__, t_predict_us/1000.0f, t_predict_us/1000.0f/step_ix);
    printf("%s:    total time = %8.2f ms\n", __func__, (t_main_end_us - t_main_start_us)/1000.0f);

    return out_coarse;
}

bark_codes bark_forward_fine_encoder(
    const bark_codes & tokens,
    const gpt_model model,
    std::mt19937 & rng,
    const int n_threads,
    const float temp) {
    // input shape: (N, n_codes)

    bark_codes input = tokens;
    std::vector<std::vector<float>> logits;

    bark_progress progress;
    progress.func = __func__;

    int64_t t_sample_us  = 0;
    int64_t t_predict_us = 0;

    size_t mem_per_token = 0;

    const int64_t t_main_start_us = ggml_time_us();

    int n_coarse          = input[0].size();
    int original_seq_len  = input.size();
    int n_remove_from_end = 0;

    // channel padding
    for (int i = 0; i < (int) input.size(); i++) {
        for (int j = N_COARSE_CODEBOOKS; j < N_FINE_CODEBOOKS; j++) {
            input[i].push_back(CODEBOOK_SIZE);
        }
    }

    // spatial padding if sequence is too short
    if (original_seq_len < 1024) {
        n_remove_from_end = 1024 - original_seq_len;
        for (int i = original_seq_len; i < 1024; i++) {
            bark_sequence _tmp(N_FINE_CODEBOOKS, CODEBOOK_SIZE);
            input.push_back(_tmp);
        }
    }

    // dry run to estimate mem_per_token
    bark_codes toy_codes = { {0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 6}, {6, 7}, {7, 8} };
    fine_gpt_eval(model, n_threads, 2, toy_codes, logits, mem_per_token);

    int n_loops = std::max(0, (int) ceilf((input.size() - 1024)/512.f)) + 1;

    // in_arr: [seq_length, n_codes]
    bark_codes in_arr = input;

    for (int n = 0; n < n_loops; n++) {
        int start_ix          = std::min(n * 512, (int) in_arr.size() - 1024);
        int start_fill_ix     = std::min(n * 512, (int) in_arr.size() - 512);
        int rel_start_fill_ix = start_fill_ix - start_ix;

        // in_buffer: [n_codes, seq_length]
        bark_codes in_buffer;
        for (int i = 0; i < N_FINE_CODEBOOKS; i++) {
            bark_sequence _buf;
            for (int j = start_ix; j < start_ix + 1024; j++) {
                _buf.push_back(in_arr[j][i]);
            }
            in_buffer.push_back(_buf);
        }

        for (int nn = n_coarse; nn < N_FINE_CODEBOOKS; nn++) {
            int64_t t_predict_start_us = ggml_time_us();
            fine_gpt_eval(model, n_threads, nn, in_buffer, logits, mem_per_token);
            t_predict_us += (ggml_time_us() - t_predict_start_us);

            for (int i = 0; i < (int) logits.size(); i++) {
                logits[i].resize(CODEBOOK_SIZE);

                int64_t t_sample_start_us = ggml_time_us();
                bark_vocab::id next = gpt_sample(logits[i], rng, temp, NULL);
                t_sample_us += (ggml_time_us() - t_sample_start_us);

                in_buffer[nn][rel_start_fill_ix+i] = next;
            }

            progress.callback((float) (n*(N_FINE_CODEBOOKS-n_coarse)+(nn-n_coarse))/(n_loops*(N_FINE_CODEBOOKS-n_coarse)));
        }

        // transfer over info into model_in
        for (int nn = n_coarse; nn < N_FINE_CODEBOOKS; nn++) {
            for (int j = 0; j < CODEBOOK_SIZE - rel_start_fill_ix; j++) {
                in_arr[start_fill_ix+j][nn] = in_buffer[nn][rel_start_fill_ix+j];
            }
        }

    }

    if (n_remove_from_end > 0) {
        in_arr.resize(in_arr.size() - n_remove_from_end);
    }

    BARK_ASSERT(tokens.size() == in_arr.size());

    const int64_t t_main_end_us = ggml_time_us();

    printf("\n\n");
    printf("%s: mem per token = %8.2f MB\n", __func__, mem_per_token/1000.0f/1000.0f);
    printf("%s:   sample time = %8.2f ms\n", __func__, t_sample_us/1000.0f);
    printf("%s:  predict time = %8.2f ms\n", __func__, t_predict_us/1000.0f);
    printf("%s:    total time = %8.2f ms\n", __func__, (t_main_end_us - t_main_start_us)/1000.0f);

    return in_arr;
}

bool encodec_eval(
        const bark_codes    & tokens,
        const encodec_model & model,
        audio_arr_t         & audio_arr,
        size_t & mem_per_token) {
    // input shape: [seq_length, n_codes]
    const int N       = tokens.size();
    const int n_codes = tokens[0].size();

    bark_codes input = tokens;

    static size_t buf_size = 256u*1024*1024;
    static void * buf = malloc(buf_size);

    if (mem_per_token > 0 && mem_per_token*N*n_codes > buf_size) {
        const size_t buf_size_new = 1.1*(mem_per_token*N*n_codes);  // add 10% to account for ggml object overhead

        // reallocate
        buf_size = buf_size_new;
        buf = realloc(buf, buf_size);
        if (buf == nullptr) {
            fprintf(stderr, "%s: failed to allocate %zu bytes\n", __func__, buf_size);
            return false;
        }
    }

    struct ggml_init_params params = {
        /*.mem_size   =*/ buf_size,
        /*.mem_buffer =*/ buf,
        /*.no_alloc   =*/ false,
    };

    struct ggml_context * ctx0 = ggml_init(params);
    struct ggml_cgraph gf = {};

    struct ggml_tensor * codes = ggml_new_tensor_2d(ctx0, GGML_TYPE_I32, N, n_codes);
    for (int c = 0; c < n_codes; c++) {
        bark_sequence _tmp;
        for (int i = 0; i < N; i++)
            _tmp.push_back(input[i][c]);
        int offset = ggml_element_size(codes)*c*N;
        memcpy((void *) ((char *) codes->data + offset), _tmp.data(), N*ggml_element_size(codes));
    }

    struct ggml_tensor * quantized_out = encodec_quantizer_decode_eval(ctx0, model, codes);
    struct ggml_tensor * output        = encodec_decoder_eval(ctx0, model, quantized_out);

    ggml_build_forward_expand(&gf, output);
    // TODO: adapt ggml_conv_1d and ggml_conv_trans_1d implementation to use multiple
    // threads.
    ggml_graph_compute_with_ctx(ctx0, &gf, 1);

    int out_seq_length = output->ne[0];
    audio_arr.resize(out_seq_length);
    memcpy(audio_arr.data(), (float *) ggml_get_data(output), sizeof(float)*out_seq_length);

    if (mem_per_token == 0) {
        mem_per_token = ggml_used_mem(ctx0)/N/n_codes;
    }

    ggml_free(ctx0);

    return true;
}

audio_arr_t bark_forward_encodec(const bark_codes & tokens, const encodec_model model) {
    audio_arr_t audio_arr;

    int64_t t_predict_us = 0;

    const int64_t t_main_start_us = ggml_time_us();

    // dry run to estimate mem_per_token
    size_t mem_per_token = 0;
    bark_codes toy_data;
    for (int i = 0; i < 20; i++) {
        bark_sequence _tmp(4, i);
        toy_data.push_back(_tmp);
    }
    encodec_eval(toy_data, model, audio_arr, mem_per_token);

    int64_t t_predict_start_us = ggml_time_us();
    encodec_eval(tokens, model, audio_arr, mem_per_token);
    t_predict_us += (ggml_time_us() - t_predict_start_us);

    const int64_t t_main_end_us = ggml_time_us();

    printf("\n\n");
    printf("%s: mem per token = %zu bytes\n", __func__, mem_per_token);
    printf("%s:  predict time = %8.2f ms / %.2f ms per token\n", __func__, t_predict_us/1000.0f, t_predict_us/1000.0f);
    printf("%s:    total time = %8.2f ms\n", __func__, (t_main_end_us - t_main_start_us)/1000.0f);

    return audio_arr;
}

int write_wav_on_disk(audio_arr_t& audio_arr, std::string dest_path) {
    drwav_data_format format;
    format.container     = drwav_container_riff;
    format.format        = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels      = 1;
    format.sampleRate    = SAMPLE_RATE;
    format.bitsPerSample = 32;

    drwav wav;
    drwav_init_file_write(&wav, dest_path.c_str(), &format, NULL);
    drwav_uint64 frames = drwav_write_pcm_frames(&wav, audio_arr.size(), audio_arr.data());
    drwav_uninit(&wav);

    fprintf(stderr, "Number of frames written = %lld.\n", frames);

    return 0;
}

bool bark_generate_audio(
        bark_model model,
        const bark_vocab& vocab,
        const char * text,
        const int n_threads,
        const int32_t seed,
        const std::string& dest_wav_path) {
    // TODO move into params
    // const int top_k = 10;
    // const int seed  = 0;

    // const float top_p     = 0.2;
    const float temp      = 0.7;
    const float fine_temp = 0.5;

    const int sliding_window_size = 60;
    const int max_coarse_history = 630;

    const float min_eos_p = 0.2;

    std::mt19937 rng(seed);

    // tokenize input (bert tokenizer)
    int32_t block_size = model.text_model.hparams.block_size;
    bark_sequence tokens = bark_tokenize_input(text, vocab, block_size);

    printf("%s: prompt: '%s'\n", __func__, text);
    printf("%s: number of tokens in prompt = %zu, first 8 tokens: ", __func__, tokens.size());
    for (int i = 0; i < std::min(8, (int) tokens.size()); i++) {
        printf("%d ", tokens[i]);
    }

    printf("\n");

    bark_sequence semantic_tokens = bark_forward_text_encoder(
            tokens, model.text_model, rng, n_threads, temp, min_eos_p);
    printf("\n");

    bark_codes coarse_tokens = bark_forward_coarse_encoder(
            semantic_tokens, model.coarse_model, rng, n_threads, temp, max_coarse_history, sliding_window_size);
    printf("\n");

    bark_codes fine_tokens = bark_forward_fine_encoder(
            coarse_tokens, model.fine_model, rng, n_threads, fine_temp);
    printf("\n");

    audio_arr_t audio_arr = bark_forward_encodec(fine_tokens, model.codec_model);
    printf("\n");

    if (dest_wav_path != "") {
        write_wav_on_disk(audio_arr, dest_wav_path);
    }

    return true;
}

bool bark_params_parse(int argc, char ** argv, bark_params & params) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-t" || arg == "--threads") {
            params.n_threads = std::stoi(argv[++i]);
        } else if (arg == "-p" || arg == "--prompt") {
            params.prompt = argv[++i];
        } else if (arg == "-m" || arg == "--model") {
            params.model = argv[++i];
        } else if (arg == "-s" || arg == "--seed") {
            params.seed = std::stoi(argv[++i]);
        } else if (arg == "-o" || arg == "--outwav") {
            params.dest_wav_path = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            bark_print_usage(argv, params);
            exit(0);
        } else {
            fprintf(stderr, "error: unknown argument: %s\n", arg.c_str());
            bark_print_usage(argv, params);
            exit(0);
        }
    }

    return true;
}

void bark_print_usage(char ** argv, const bark_params & params) {
    fprintf(stderr, "usage: %s [options]\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  -h, --help            show this help message and exit\n");
    fprintf(stderr, "  -t N, --threads N     number of threads to use during computation (default: %d)\n", params.n_threads);
    fprintf(stderr, "  -s N, --seed N        seed for random number generator (default: %d)\n", params.seed);
    fprintf(stderr, "  -p PROMPT, --prompt PROMPT\n");
    fprintf(stderr, "                        prompt to start generation with (default: random)\n");
    fprintf(stderr, "  -m FNAME, --model FNAME\n");
    fprintf(stderr, "                        model path (default: %s)\n", params.model.c_str());
    fprintf(stderr, "  -o FNAME, --outwav FNAME\n");
    fprintf(stderr, "                        output generated wav (default: %s)\n", params.dest_wav_path.c_str());
    fprintf(stderr, "\n");
}
