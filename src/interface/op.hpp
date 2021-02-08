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

#ifndef INTERFACE_OP_HPP
#define INTERFACE_OP_HPP

#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "attribute_value.hpp"
#include "c_types_map.hpp"
#include "internal_ops.hpp"
#include "logical_tensor.hpp"

struct dnnl_graph_op {
public:
    const static size_t DEFAULT_ID = std::numeric_limits<size_t>::max();

    // create dnnl_graph_op with explicit id, kind, and string
    dnnl_graph_op(size_t id, dnnl::graph::impl::op_kind_t kind,
            std::string debug_string)
        : id_ {id}, kind_ {kind}, debug_string_ {std::move(debug_string)} {}

    // create dnnl_graph_op with default id, only for internal use.
    dnnl_graph_op(dnnl::graph::impl::op_kind_t kind, std::string debug_string)
        : dnnl_graph_op(DEFAULT_ID, kind, debug_string) {}

    ~dnnl_graph_op() = default;

    dnnl::graph::impl::op_kind_t kind() const { return kind_; }

    size_t id() const { return id_; }

    const std::string &debug() const { return debug_string_; }

    void add_input(const dnnl::graph::impl::logical_tensor_t *t) {
        inputs_.push_back(*t);
    }

    const std::vector<dnnl::graph::impl::logical_tensor_t> &inputs() const {
        return inputs_;
    }

    void add_output(const dnnl::graph::impl::logical_tensor_t *t) {
        outputs_.push_back(*t);
    }

    const std::vector<dnnl::graph::impl::logical_tensor_t> &outputs() const {
        return outputs_;
    }

    dnnl::graph::impl::status_t kind_of(const std::string &name,
            dnnl::graph::impl::attribute_kind_t &kind) const {
        const auto &found = attributes_.find(name);
        if (found == end(attributes_)) {
            return dnnl::graph::impl::status::invalid_argument;
        }

        kind = found->second.get_kind();
        return dnnl::graph::impl::status::success;
    }

    template <typename Attr>
    dnnl_graph_op &set_attr(const std::string &name, Attr &&a) {
        auto it = attributes_.find(name);
        if (it != end(attributes_)) {
            it->second = {a};
        } else {
            attributes_.insert({name, {a}});
        }
        return *this;
    }

    dnnl_graph_op &set_attr(const std::string &name,
            const dnnl::graph::impl::attribute_value &a) {
        auto it = attributes_.find(name);
        if (it != end(attributes_)) {
            it->second = a;
        } else {
            attributes_.insert({name, a});
        }
        return *this;
    }

    template <typename Attr>
    dnnl::graph::impl::status_t get_attr(
            const std::string &name, const Attr **attr) const {
        const auto &found = attributes_.find(name);
        if (found == end(attributes_)) {
            return dnnl::graph::impl::status::invalid_argument;
        }

        Attr &val = found->second.get<Attr>();
        *attr = &val;
        return dnnl::graph::impl::status::success;
    }

    const std::map<std::string, dnnl::graph::impl::attribute_value> &
    get_attributes() const {
        return attributes_;
    }

    static const std::string &kind2str(dnnl::graph::impl::op_kind_t kind) {
        // 0: Abs, ..., N: LastSymbol, 0x1234: any, ...
        const size_t k = static_cast<size_t>(kind);
        const size_t l
                = static_cast<size_t>(dnnl::graph::impl::op_kind::LastSymbol);
        const size_t a = static_cast<size_t>(dnnl::graph::impl::op_kind::any);
        if (k <= l) {
            return dnnl::graph::impl::op_kind::op_kind_strings.at(k);
        } else {
            return dnnl::graph::impl::op_kind::internal_op_strings.at(k - a);
        }
    }

private:
    size_t id_ {};
    dnnl::graph::impl::op_kind_t kind_ {};
    std::string debug_string_ {};
    std::vector<dnnl::graph::impl::logical_tensor_t> inputs_ {};
    std::vector<dnnl::graph::impl::logical_tensor_t> outputs_ {};
    std::map<std::string, dnnl::graph::impl::attribute_value> attributes_;
};

#endif
