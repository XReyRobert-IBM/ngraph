//*****************************************************************************
// Copyright 2017-2018 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "ngraph/function.hpp"
#include "ngraph/ngraph.hpp"
#include "ngraph/pass/liveness.hpp"
#include "ngraph/pass/manager.hpp"
#include "util/test_tools.hpp"

using namespace std;
using namespace ngraph;

TEST(tensor, size)
{
    pass::Manager pass_manager;

    pass_manager.register_pass<pass::Liveness>();

    {
        auto arg0 = make_shared<op::Parameter>(element::f32, Shape{2, 3});
        auto add = make_shared<op::Add>(arg0, arg0);
        auto f0 = make_shared<Function>(add, ParameterVector{arg0});

        pass_manager.run_passes(f0);

        auto& outputs = arg0->get_outputs();
        ASSERT_EQ(1, outputs.size());
        descriptor::Tensor& output = outputs[0].get_tensor();
        EXPECT_EQ(2 * 3 * 4, output.size());
    }

    {
        auto arg0 = make_shared<op::Parameter>(element::f32, Shape{});
        auto add = make_shared<op::Add>(arg0, arg0);
        auto f0 = make_shared<Function>(add, ParameterVector{arg0});

        pass_manager.run_passes(f0);

        auto& outputs = arg0->get_outputs();
        ASSERT_EQ(1, outputs.size());
        descriptor::Tensor& output = outputs[0].get_tensor();
        EXPECT_EQ(1 * 4, output.size());
    }

    {
        auto arg0 = make_shared<op::Parameter>(element::f32, Shape{1});
        auto add = make_shared<op::Add>(arg0, arg0);
        auto f0 = make_shared<Function>(add, ParameterVector{arg0});

        pass_manager.run_passes(f0);

        auto& outputs = arg0->get_outputs();
        ASSERT_EQ(1, outputs.size());
        descriptor::Tensor& output = outputs[0].get_tensor();
        EXPECT_EQ(1 * 4, output.size());
    }
}

template <typename T>
void test_read_write(const vector<T>& x)
{
    auto backend = runtime::Backend::create("INTERPRETER");

    auto a = backend->create_tensor(element::from<T>(), Shape{2, x.size()});

    vector<T> result(2 * x.size());

    a->write(&x[0], 0, x.size() * sizeof(T));
    copy(x.begin(), x.end(), result.begin());
    a->write(&x[0], x.size() * sizeof(T), x.size() * sizeof(T));
    copy(x.begin(), x.end(), result.begin() + x.size());

    vector<T> af_vector(2 * x.size());
    a->read(af_vector.data(), 0, af_vector.size() * sizeof(T));
    ASSERT_EQ(af_vector, result);

    vector<T> result1(x.size());
    vector<T> result2(x.size());
    copy(result.begin() + 1, result.begin() + 1 + x.size(), result1.begin());
    a->read(&result2[0], sizeof(T), sizeof(T) * x.size());
    ASSERT_EQ(result1, result2);
}

template <typename T>
void test_copy_to_same_backend(const vector<T>& x)
{
    auto backend_int = runtime::Backend::create("INTERPRETER");
    auto a = backend_int->create_tensor(element::from<T>(), Shape{(x.size() / 2), (x.size() / 2)});
    auto b = backend_int->create_tensor(element::from<T>(), Shape{(x.size() / 2), (x.size() / 2)});

    vector<T> result(x.size());

    a->write(&x[0], 0, x.size() * sizeof(T));
    a->read(result.data(), 0, result.size() * sizeof(T));

    auto s = a->get_size_in_bytes();
    a->copy_to(b, 0, s);

    vector<T> af_vector(x.size());
    b->read(af_vector.data(), 0, af_vector.size() * sizeof(T));
    ASSERT_EQ(af_vector, result);
}

template <typename T>
void test_copy_to_other_backend(const vector<T>& x)
{
    auto backend_gpu = runtime::Backend::create("GPU");
    auto backend_int = runtime::Backend::create("INTERPRETER");
    auto a = backend_gpu->create_tensor(element::from<T>(), Shape{(x.size() / 2), (x.size() / 2)});
    auto b = backend_int->create_tensor(element::from<T>(), Shape{(x.size() / 2), (x.size() / 2)});

    vector<T> result(x.size());

    a->write(&x[0], 0, x.size() * sizeof(T));
    a->read(result.data(), 0, result.size() * sizeof(T));

    auto s = a->get_size_in_bytes();
    a->copy_to(b, 0, s);

    vector<T> af_vector(x.size());
    b->read(af_vector.data(), 0, af_vector.size() * sizeof(T));
    ASSERT_EQ(af_vector, result);
}

#if defined(NGRAPH_INTERPRETER_ENABLE)
TEST(tensor, read_write)
{
    test_read_write<float>({1.0, 3.0, 5.0});
    test_read_write<int64_t>({-1, 2, 4});
}

TEST(tensor, copy_to_same_backend)
{
    test_copy_to_same_backend<float>({1.2, 3.0, 5.0, 6.2, 7.0, 8.0});
    test_copy_to_same_backend<int64_t>({-1, 2, 4, -5, 3, 6});
}
#endif

#if defined(NGRAPH_GPU_ENABLE) && defined(NGRAPH_INTERPRETER_ENABLE)
TEST(tensor, copy_to_other_backend)
{
    test_copy_to_other_backend<float>({1.2, 3.0, 5.0, 6.2, 7.0, 8.0});
    test_copy_to_other_backend<int64_t>({-1, 2, 4, -5, 3, 6});
}
#endif

TEST(tensor, output_flag)
{
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::Liveness>();

    auto arg0 = make_shared<op::Parameter>(element::f32, Shape{1});
    auto add = make_shared<op::Add>(arg0, arg0);
    auto f0 = make_shared<Function>(add, ParameterVector{arg0});

    pass_manager.run_passes(f0);

    for (size_t i = 0; i < f0->get_output_size(); ++i)
    {
        EXPECT_TRUE(f0->get_output_op(i)->is_output());
    }
}
