/* NodeArrayOutputOLA.hpp: Headers for Aniray system OLA output
 *
 * Created by Perry Naseck on 2022-08-24.
 *
 * This file is a part of Aniray
 * https://github.com/HypersonicED/aniray
 *
 * Copyright (c) 2022, Hypersonic
 * Copyright (c) 2022, Perry Naseck
 * All rights reserved.
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
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ANIRAY_NODEARRAYOUTPUTOLA_HPP
#define ANIRAY_NODEARRAYOUTPUTOLA_HPP

#include <ola/Clock.h>
#include <ola/DmxBuffer.h>
#include <ola/Logging.h>
#include <ola/client/ClientWrapper.h>
#include <ola/io/SelectServer.h>
#include <ola/thread/Thread.h>

#include <aniray/DMXAddr.hpp>
#include <aniray/NodeArrayOutput.hpp>
#include <boost/log/core/record.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/trivial.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace aniray {

using std::int64_t;
using std::size_t;
using std::uint32_t;
using std::uint8_t;

class NodeArrayOutputOLAThread : public ola::thread::Thread {
   public:
    NodeArrayOutputOLAThread(const Options& options);

    auto Start(const ola::TimeInterval& period,
               std::unordered_map<uint32_t, size_t>& universesToBuffers)
        -> bool;
    void Stop();
    auto GetSelectServer() -> ola::io::SelectServer*;
    void updateData(std::vector<ola::DmxBuffer> buffers);

   protected:
    auto Run() -> void* override;

   private:
    ola::client::OlaClientWrapper mOLAClientWrapper;
    ola::TimeInterval mPeriod;
    std::unordered_map<uint32_t, size_t> mUniversesToBuffers;
    std::vector<ola::DmxBuffer> mBuffers;
    std::mutex mUpdateMutex;

    auto InternalSendUniverses() -> bool;
};

template <typename NodeArrayT, auto DataToOutput>
class NodeArrayOutputOLA : NodeArrayOutput<NodeArrayT, DataToOutput> {
   public:
    using InnerNodeArrayT = NodeArrayT;
    using NodeArrayOutput<NodeArrayT, DataToOutput>::updateAndSend;
    using NodeArrayOutput<NodeArrayT, DataToOutput>::nodeArray;

    NodeArrayOutputOLA(NodeArrayT& nodes, int64_t interval)
        : NodeArrayOutput<NodeArrayT, DataToOutput>::NodeArrayOutput(nodes) {
        using NodeT = typename InnerNodeArrayT::InnerNodeT;
        ola::InitLogging(ola::OLA_LOG_WARN, ola::OLA_LOG_STDERR);

        for (std::shared_ptr<NodeT> node :
             NodeArrayOutput<NodeArrayT, DataToOutput>::nodeArray().nodes()) {
            DMXAddr addr = node->addr();
            if (mUniversesToBuffers.count(addr.mUniverse) < 1) {
                mBuffers.emplace_back();
                size_t i = mBuffers.size() - 1;
                mBuffers[i].Blackout();
                mUniversesToBuffers[addr.mUniverse] = i;
                BOOST_LOG_TRIVIAL(info)
                    << "NodeArrayOutputOLA: Created universe " << addr.mUniverse
                    << " buffer " << i;
            }
        }

        ola::thread::Thread::Options threadOptions("Aniray_NAOutOLA");
        mOLAThread = std::make_unique<NodeArrayOutputOLAThread>(threadOptions);
        if (!mOLAThread->Start(ola::TimeInterval(interval),
                               mUniversesToBuffers)) {
            throw std::runtime_error(
                "NodeArrayOutputOLA: Error setting up OLA thread!");
        }
        BOOST_LOG_TRIVIAL(info) << "NodeArrayOutputOLA: Connected to OLA";
    }

    ~NodeArrayOutputOLA() {
        mOLAThread->Stop();
        mOLAThread->Join();
    }

    NodeArrayOutputOLA(NodeArrayOutputOLA&) = delete;        // copy constructor
    NodeArrayOutputOLA(const NodeArrayOutputOLA&) = delete;  // copy constructor
    NodeArrayOutputOLA(NodeArrayOutputOLA&&) = delete;       // move constructor
    auto operator=(NodeArrayOutputOLA&)
        -> NodeArrayOutputOLA& = delete;  // copy assignment
    auto operator=(const NodeArrayOutputOLA&)
        -> NodeArrayOutputOLA& = delete;  // copy assignment
    auto operator=(NodeArrayOutputOLA&&) noexcept
        -> NodeArrayOutputOLA& = default;  // move assignment

   private:
    std::unordered_map<uint32_t, size_t> mUniversesToBuffers;
    std::vector<ola::DmxBuffer> mBuffers;
    std::unique_ptr<NodeArrayOutputOLAThread> mOLAThread;

    void setChannel(uint32_t universe, uint8_t channel, uint8_t data) override {
        ola::DmxBuffer& buffer = mBuffers[mUniversesToBuffers[universe]];
        buffer.SetChannel(channel, data);
    }
    auto sendData() -> bool override {
        bool res = true;
        mOLAThread->updateData(mBuffers);
        return res;
    }
};

}  // namespace aniray

#endif  // ANIRAY_NODEARRAYOUTPUTOLA_HPP
