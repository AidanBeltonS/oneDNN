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

#include "oneapi/dnnl/dnnl_graph.hpp"

#include "test_api_common.hpp"
#include "gtest/gtest.h"

#include <cstdint>

TEST(api_partition, partition_test) {
    using namespace dnnl::graph;
    engine::kind engine_kind = static_cast<engine::kind>(api_test_engine_kind);
    engine eng = cpp_api_test_dnnl_graph_engine_create(engine_kind);
    engine::kind real_engine_kind = eng.get_kind();

    // when enable sycl, the real engine kind will always be gpu, because we
    // use default gpu::selector to find gpu device.
    ASSERT_EQ(real_engine_kind, engine_kind);

    graph g(engine_kind);

    std::vector<int64_t> input_dims {8, 256, 56, 56};
    std::vector<int64_t> conv_weight_dims {64, 256, 1, 1};
    std::vector<int64_t> conv_dst_dims {8, 64, 56, 56};
    std::vector<int64_t> infer_dst_dims {-1, -1, -1, -1};

    logical_tensor lt1 {0, logical_tensor::data_type::f32, input_dims,
            logical_tensor::layout_type::undef};
    logical_tensor lt2 {1, logical_tensor::data_type::f32, conv_weight_dims,
            logical_tensor::layout_type::undef};
    logical_tensor lt3 {2, logical_tensor::data_type::f32, conv_dst_dims,
            logical_tensor::layout_type::undef};
    logical_tensor lt4 {3, logical_tensor::data_type::f32, infer_dst_dims,
            logical_tensor::layout_type::undef};

    op conv(0, op::kind::Convolution, "conv");
    op relu_op(1, op::kind::ReLU, "relu");

    conv.set_attr<std::vector<int64_t>>("strides", {1, 1});
    conv.set_attr<std::vector<int64_t>>("pads_begin", {0, 0});
    conv.set_attr<std::vector<int64_t>>("pads_end", {0, 0});
    conv.set_attr<std::vector<int64_t>>("dilations", {1, 1});
    conv.set_attr<std::string>("data_format", "NCX");
    conv.set_attr<std::string>("filter_format", "OIX");
    conv.set_attr<int64_t>("groups", 1);

    conv.add_inputs({lt1, lt2});
    conv.add_output(lt3);
    relu_op.add_input(lt3);
    relu_op.add_output(lt4);

    g.add_op(conv);
    g.add_op(relu_op);

    //create_partition
    auto partitions = g.get_partitions(partition::policy::fusion);
    ASSERT_EQ(partitions.size(), 1);

    //get_ops
    std::vector<size_t> ops = partitions[0].get_ops();
    ASSERT_EQ(ops.size(), 2);
    ASSERT_EQ(partitions[0].get_ops_num(), 2);

    logical_tensor lt1_plain {0, logical_tensor::data_type::f32, input_dims,
            logical_tensor::layout_type::strided};
    logical_tensor lt2_plain {1, logical_tensor::data_type::f32,
            conv_weight_dims, logical_tensor::layout_type::strided};
    logical_tensor lt3_plain {2, logical_tensor::data_type::f32, conv_dst_dims,
            logical_tensor::layout_type::strided};
    logical_tensor lt4_any {3, logical_tensor::data_type::f32, infer_dst_dims,
            logical_tensor::layout_type::any};

    //compile partition
    std::vector<logical_tensor> in0({lt1_plain, lt2_plain, lt3_plain});
    std::vector<logical_tensor> out0({lt4_any});

    //infer_shape
    partitions[0].infer_shape(in0, out0);
    std::vector<int64_t> infered_dst_dims = lt4_any.get_dims();
    ASSERT_EQ(out0[0].get_dims()[0], 8);
    ASSERT_EQ(out0[0].get_dims()[1], 64);
    ASSERT_EQ(out0[0].get_dims()[2], 56);
    ASSERT_EQ(out0[0].get_dims()[3], 56);

    auto cp = partitions[0].compile(in0, out0, eng);
    // query logical tensor from compiled partition
    auto lt4_opaque = cp.query_logical_tensor(3);
    ASSERT_EQ(
            lt4_opaque.get_layout_type(), logical_tensor::layout_type::opaque);
}

TEST(api_partition, get_inputs_outputs_ids) {
    using namespace dnnl::graph;
    engine::kind engine_kind = static_cast<engine::kind>(api_test_engine_kind);
    engine eng = cpp_api_test_dnnl_graph_engine_create(engine_kind);
    engine::kind real_engine_kind = eng.get_kind();

    // when enable sycl, the real engine kind will always be gpu, because we
    // use default gpu::selector to find gpu device.
    ASSERT_EQ(real_engine_kind, engine_kind);

    graph g(engine_kind);

    std::vector<int64_t> input_dims {8, 256, 56, 56};
    std::vector<int64_t> conv_weight_dims {64, 256, 1, 1};
    std::vector<int64_t> conv_dst_dims {8, 64, 56, 56};
    std::vector<int64_t> infer_dst_dims {-1, -1, -1, -1};

    std::vector<size_t> inputs_ids {0, 1};
    std::vector<size_t> output_ids {2};

    logical_tensor lt1 {inputs_ids[0], logical_tensor::data_type::f32,
            input_dims, logical_tensor::layout_type::undef};
    logical_tensor lt2 {inputs_ids[1], logical_tensor::data_type::f32,
            conv_weight_dims, logical_tensor::layout_type::undef};
    logical_tensor lt3 {output_ids[0], logical_tensor::data_type::f32,
            conv_dst_dims, logical_tensor::layout_type::undef};

    op conv(0, op::kind::Convolution, "conv");

    conv.set_attr<std::vector<int64_t>>("strides", {1, 1});
    conv.set_attr<std::vector<int64_t>>("pads_begin", {0, 0});
    conv.set_attr<std::vector<int64_t>>("pads_end", {0, 0});
    conv.set_attr<std::vector<int64_t>>("dilations", {1, 1});
    conv.set_attr<std::string>("data_format", "NCX");
    conv.set_attr<std::string>("filter_format", "OIX");
    conv.set_attr<int64_t>("groups", 1);

    conv.add_inputs({lt1, lt2});
    conv.add_output(lt3);

    g.add_op(conv);

    //create_partition
    auto partitions = g.get_partitions(partition::policy::fusion);
    ASSERT_EQ(partitions.size(), 1);

    // check ids of inputs
    std::vector<size_t> got_input_ids = partitions[0].get_inputs();
    ASSERT_EQ(got_input_ids.size(), inputs_ids.size());
    for (size_t i = 0; i < got_input_ids.size(); ++i)
        ASSERT_EQ(got_input_ids[i], inputs_ids[i]);

    // check ids of outputs
    std::vector<size_t> got_output_ids = partitions[0].get_outputs();
    ASSERT_EQ(got_output_ids.size(), output_ids.size());
    for (size_t i = 0; i < got_output_ids.size(); ++i)
        ASSERT_EQ(got_output_ids[i], output_ids[i]);

    // check partition's supported flag
    ASSERT_FALSE(partitions[0].is_supported());
}
