/*******************************************************************************
* Copyright 2020-2021 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef BACKEND_DNNL_OPERATORS_LAYERNORM_HPP
#define BACKEND_DNNL_OPERATORS_LAYERNORM_HPP

#include <vector>
#include <unordered_map>

#include "backend/dnnl/tensor.hpp"

namespace dnnl {
namespace graph {
namespace impl {
namespace dnnl_impl {

namespace layernorm {
enum layernorm_inputs { kSrc, kScale, kShift };
enum layernorm_outputs { kDst, kMean, kVariance };
} // namespace layernorm

struct layer_normalization_forward : public dnnl::layer_normalization_forward,
                                     public kernel_base {
    using super = dnnl::layer_normalization_forward;

private:
    primitive_desc pd_;
    float epsilon_ = 0.00001f;
    //todo(jihui):need to support begin_norm_axis_
    int64_t begin_norm_axis_ = -1;
    bool use_affine_ = true;
    bool keep_stats_ = true;

    tensor scale_shift_;
    tensor expected_src_;
    tensor expected_dst_;
    tensor expected_mean_;
    tensor expected_variance_;

    dnnl::engine p_engine_;
    dnnl::stream p_stream_;

public:
    void compute(tensor &scale, tensor &shift, impl::allocator_t *alc) {
        if (use_affine_) {
            if (scale_shift_.is_empty()) {
                scale_shift_ = tensor {pd_.weights_desc(), p_engine_, alc};
            }

            auto *scale_shift_buf
                    = static_cast<char *>(scale_shift_.get_data_handle());
#if DNNL_GRAPH_WITH_SYCL
            cl::sycl::queue q = dnnl::sycl_interop::get_queue(p_stream_);
            q.memcpy(
                    scale_shift_buf, scale.get_data_handle(), scale.get_size());
            q.memcpy(scale_shift_buf + scale.get_size(),
                    shift.get_data_handle(), shift.get_size());
#else
            std::memcpy(
                    scale_shift_buf, scale.get_data_handle(), scale.get_size());
            std::memcpy(scale_shift_buf + scale.get_size(),
                    shift.get_data_handle(), shift.get_size());
#endif
        }

        exec_args ln_args;

        ln_args.insert({DNNL_ARG_SRC, expected_src_});
        ln_args.insert({DNNL_ARG_DST, expected_dst_});
        if (use_affine_) ln_args.insert({DNNL_ARG_SCALE_SHIFT, scale_shift_});
        if (keep_stats_) {
            ln_args.insert({DNNL_ARG_MEAN, expected_mean_});
            ln_args.insert({DNNL_ARG_VARIANCE, expected_variance_});
        }

        super(pd_).execute(p_stream_, ln_args);
    }

    impl::status_t compile_impl(const impl::op_t *op,
            const impl::engine_t *g_engine,
            const std::vector<impl::logical_tensor_t> &inputs,
            const std::vector<impl::logical_tensor_t> &outputs) override {
        using desc = tensor::desc;
        // prepare the inputs and outputs' tensors' descs
        const desc src {inputs.at(layernorm::kSrc)};
        const desc dst {outputs.at(layernorm::kDst)};

        if (op->has_attr("epsilon")) epsilon_ = op->get_attr<float>("epsilon");
        if (op->has_attr("begin_norm_axis"))
            begin_norm_axis_ = op->get_attr<int64_t>("begin_norm_axis");
        if (op->has_attr("keep_stats"))
            keep_stats_ = op->get_attr<bool>("keep_stats");
        if (op->has_attr("use_affine"))
            use_affine_ = op->get_attr<bool>("use_affine");

        p_engine_ = make_dnnl_engine(*g_engine);
        normalization_flag flags = normalization_flag::none;

        if (use_affine_) flags = normalization_flag::use_scale_shift;

        if (keep_stats_)
            pd_ = primitive_desc(
                    {prop_kind::forward_training, src, epsilon_, flags},
                    p_engine_);
        else
            pd_ = primitive_desc(
                    {prop_kind::forward_inference, src, epsilon_, flags},
                    p_engine_);

        const tensor::desc optimal_dst_desc {pd_.dst_desc()};

        impl::logical_tensor_t *ori_dst_lt
                = const_cast<impl::logical_tensor_t *>(
                        &outputs.at(layernorm::kDst));
        fill_layout_info(ori_dst_lt, optimal_dst_desc);

        if (keep_stats_) {
            if (outputs.size() < 3) {
                BACKEND_DNNL_ENFORCE(
                        0, "Wrong output number for layernorm compile");
            }
            const tensor::desc optimal_mean_desc {pd_.mean_desc()};
            impl::logical_tensor_t *ori_mean_lt
                    = const_cast<impl::logical_tensor_t *>(
                            &outputs.at(layernorm::kMean));
            fill_layout_info(ori_mean_lt, optimal_mean_desc);

            const tensor::desc optimal_var_desc {pd_.variance_desc()};
            impl::logical_tensor_t *ori_var_lt
                    = const_cast<impl::logical_tensor_t *>(
                            &outputs.at(layernorm::kVariance));
            fill_layout_info(ori_var_lt, optimal_var_desc);
        }
        return impl::status::success;
    }

    impl::status_t execute_impl(const impl::op_t *op,
            const impl::stream_t *g_stream,
            const std::vector<impl::tensor_t> &inputs,
            const std::vector<impl::tensor_t> &outputs) override {
        UNUSED(op);
        p_stream_ = make_dnnl_stream(p_engine_, *g_stream);
        impl::allocator_t *alc = g_stream->get_engine()->get_allocator();

        tensor src {inputs.at(layernorm::kSrc), p_engine_, alc};
        if (src.get_desc() != pd_.src_desc()) {
            if (expected_src_.is_empty()) {
                expected_src_ = tensor {pd_.src_desc(), p_engine_, alc};
            }
            src.reorder_to(p_stream_, expected_src_);
        } else {
            expected_src_ = src;
        }

        tensor scale;
        tensor shift;
        if (use_affine_) {
            if (inputs.size() < 3) {
                BACKEND_DNNL_ENFORCE(
                        0, "Wrong input number for layernorm execute");
            }
            scale = tensor {inputs.at(layernorm::kScale), p_engine_, alc};
            shift = tensor {inputs.at(layernorm::kShift), p_engine_, alc};
        }
        tensor dst {outputs.at(layernorm::kDst), p_engine_, alc};
        tensor mean;
        tensor variance;
        if (dst.get_desc() != pd_.dst_desc()) {
            if (expected_dst_.is_empty()) {
                expected_dst_ = tensor {pd_.dst_desc(), p_engine_, alc};
            }
        } else
            expected_dst_ = dst;

        if (keep_stats_) {
            if (outputs.size() < 3) {
                BACKEND_DNNL_ENFORCE(
                        0, "Wrong output number for layernorm execute");
            }
            mean = tensor {outputs.at(layernorm::kMean), p_engine_, alc};
            variance
                    = tensor {outputs.at(layernorm::kVariance), p_engine_, alc};

            if (mean.get_desc() != pd_.mean_desc()) {
                if (expected_mean_.is_empty()) {
                    expected_mean_ = tensor {pd_.dst_desc(), p_engine_, alc};
                }
            } else
                expected_mean_ = mean;

            if (variance.get_desc() != pd_.variance_desc()) {
                if (expected_variance_.is_empty()) {
                    expected_variance_
                            = tensor {pd_.dst_desc(), p_engine_, alc};
                }
            } else
                expected_variance_ = variance;
        }

        compute(scale, shift, alc);

        if (expected_dst_ != dst) expected_dst_.reorder_to(p_stream_, dst);

        if (keep_stats_) {
            if (expected_mean_ != mean)
                expected_mean_.reorder_to(p_stream_, mean);
            if (expected_variance_ != variance)
                expected_variance_.reorder_to(p_stream_, variance);
        }
        return impl::status::success;
    }
};

struct layer_normalization_backward
    : public dnnl::layer_normalization_backward {
    static void compute() {}
};

} // namespace dnnl_impl
} // namespace impl
} // namespace graph
} // namespace dnnl

#endif
