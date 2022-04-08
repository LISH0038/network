#include <salticidae/network.h>
#include <set>
#include <map>
#include <iostream>
#include <utility>
#include <queue>

using salticidae::PeerId;
using salticidae::PeerNetwork;
using salticidae::NetAddr;
using salticidae::DataStream;
using salticidae::htole;
using salticidae::letoh;
using Net = PeerNetwork<uint8_t>;
using std::placeholders::_1;
using std::placeholders::_2;

/** Hello Message. */
struct MsgBlock {
    static const uint8_t opcode = 0x0;
    DataStream serialized;
    std::string name;
    std::string text;
    uint32_t msg_id;
    uint32_t blk_id;
    /** Defines how to serialize the msg. */
    MsgBlock(const std::string &name,
             const std::string &text,
             uint32_t msg_id,
             uint32_t blk_id) {
        serialized << msg_id;
        serialized << blk_id;
        serialized << htole((uint32_t)name.length());
        serialized << name << text;
    }
    /** Defines how to parse the msg. */
    MsgBlock(DataStream &&s) {
        s >> msg_id;
        s >> blk_id;
        uint32_t len;
        s >> len;
        len = letoh(len);
        name = std::string((const char *)s.get_data_inplace(len), len);
        len = 20;
        text = std::string((const char *)s.get_data_inplace(len), len);
    }
};

/** Acknowledgement Message. */
struct MsgAck {
    static const uint8_t opcode = 0x1;
    DataStream serialized;
    uint32_t nid;
    uint32_t mid;
    MsgAck(const int mid, const int nid) {
        serialized << mid;
        serialized << nid;
    }
    MsgAck(DataStream &&s) {
        s >> mid;
        s >> nid;
    }
};

class MyNet: public Net {
    int fanout =2;
    uint32_t msgCounter = 0;
    std::map<int, salticidae::PeerId> peerIdMap;
    // msg_id:  <list of nid>
    std::map<uint32_t, std::unordered_set<uint32_t>> ackSet;
    std::map<uint32_t, std::unordered_set<uint32_t>> received;
    std::queue<std::pair<MsgBlock, salticidae::PeerId>> to_send_buffer;

    void on_receive_hello(MsgBlock &&msg, const MyNet::conn_t &conn) {
        printf("[%s] %s says %s, msg_id = %d, blk_id= %d\n", name.c_str(), msg.name.c_str(), msg.text.c_str(), msg.msg_id, msg.blk_id);

        /* send relay msg */
        if (ackSet.find(msg.msg_id) != ackSet.end()) {
            std::unordered_set<uint32_t> acks = ackSet[msg.msg_id];
            for (int tid = id*fanout+1; tid < (id +1)*fanout+1; ++tid) {
                if (acks.find(tid) == acks.end() && peerIdMap.find(tid) != peerIdMap.end()) {
                    const auto &peerId = peerIdMap[tid];
                    printf("[%s]  relay msg to %d, peer_port =%d\n", name.c_str(), tid, get_peer_conn(peerId)->get_peer_addr().port);
                    send_msg(MsgBlock(name, "This is a relay msg!", msg.msg_id, msg.blk_id), peerId);
                }
            }
        } else {
            for (int tid = id*fanout+1; tid < (id +1)*fanout+1; ++tid) {
                if (peerIdMap.find(tid) != peerIdMap.end()) {
                    const auto &peerId = peerIdMap[tid];
                    printf("[%s]  relay msg to %d, peer_port =%d\n", name.c_str(), tid, get_peer_conn(peerId)->get_peer_addr().port);
                    send_msg(MsgBlock(name, "This is a relay msg!", msg.msg_id, msg.blk_id), peerId);
                }
            }
        }

        if (!received_complete_msg(msg.msg_id, msg.blk_id)) {
            return;
        }

        /* send acknowledgement */
        send_msg(MsgAck(msg.msg_id, id), peerIdMap[0]);
        send_msg(MsgAck(msg.msg_id, id), conn);
    }

    void on_receive_ack(MsgAck &&msg, const MyNet::conn_t &conn) {
        auto net = static_cast<MyNet *>(conn->get_net());
        printf("[%s] receive ack from %d\n", net->name.c_str(), msg.nid);
        ackSet[msg.mid].insert(msg.nid);
//        /* notify parent */
//        if (id != 0) {
//            int pid = id/fanout;
//            const auto &peerId = peerIdMap[pid];
//            send_msg(MsgAck(msg.mid, msg.nid), peerId);
//        }
    }
public:
    MyNet(const salticidae::EventContext &ec,
          const MyNet::Config &config,
          int id,
          std::string  name):
            Net(ec, config),
            id(id),
            name(std::move(name)) {
        /* message handler could be a bound method */
        reg_handler(salticidae::generic_bind(&MyNet::on_receive_hello, this, _1, _2));
        reg_handler(salticidae::generic_bind(&MyNet::on_receive_ack, this, _1, _2));
    }

    auto add_peer(const salticidae::PeerId &peerId, int numid) {
        peerIdMap[numid] = peerId;
        return Net::add_peer(peerId);
    }

    uint32_t trigger() {
        uint32_t mid = msgCounter++;
        printf("[%s] let's do a broadcast!\n", name.c_str());
//        for (const auto &peerId : peerIdMap) {
//            printf("[%s] send to %d\n", name.c_str(), peerId.first);
//            send_msg(MsgBlock(name, "This is a flat broadcast!", msg_id), peerId.second);
//        }
        for (int tid = id*fanout+1; tid < (id +1)*fanout+1; ++tid) {
            if (peerIdMap.find(tid) != peerIdMap.end()) {
                const auto &peerId = peerIdMap[tid];
                printf("[%s] send to %d, peer_port =%d\n", name.c_str(), tid, get_peer_conn(peerId)->get_peer_addr().port);
                for (int i = 0; i < 3; ++i) {
                    to_send_buffer.push(std::pair<MsgBlock, salticidae::PeerId>(MsgBlock(name, "This is a broadcast!", mid, i), peerId));
                }
//                send_msg_by_block();
            }
        }
        return mid;
    }

    void send_msg_by_block() {
//        std::vector<char> text(1000, '.'); std::string(text.begin(), text.end())；
        if (to_send_buffer.empty()) {
            return;
        }
        std::pair<MsgBlock, salticidae::PeerId> pair = to_send_buffer.front();
        send_msg(pair.first, pair.second);
        to_send_buffer.pop();
    }

    bool received_complete_msg(uint32_t msg_id, uint32_t blk_id) {
        received[msg_id].insert(blk_id);
        if (received[msg_id].size() == 3) {
            return true;
        }
        return false;
    }

    void trigger_backup(uint32_t mid) {
        printf("[%s] trigger backup!\n", name.c_str());
        if (ackSet.find(mid) != ackSet.end()) {
            std::unordered_set<uint32_t> acks = ackSet[mid];
            for (const auto &peerId : peerIdMap) {
                if (acks.find(peerId.first) == acks.end()) {
                    printf("[%s] send to backup child %d\n", name.c_str(), peerId.first);
                    for (int i = 0; i < 3; ++i) {
                        send_msg(MsgBlock(name, "This is a broadcast!", mid, i), peerId.second);
                    }
                }
            } // to clean old msg
        } // else, keep waiting or send to all again?
        else {
            for (const auto &peerId : peerIdMap) {
                printf("[%s] send to backup child %d\n", name.c_str(), peerId.first);
                for (int i = 0; i < 3; ++i) {
                    send_msg(MsgBlock(name, "This is a broadcast!", mid, i), peerId.second);
                }
            }
        }
    }
//    std::set<PeerId> childPeers;
//    std::unordered_set<uint256_t> valid_tls_certs;
//    std::vector<PeerId> peers;
//    /** network stack */
//
    const int id;
    const std::string name;
};

int main(int argc, char* argv[]) {
//    std::map<int, std::vector<int>> tree{{0, {1,2}},{1, {3,4}},{2, {5,6}}};
    int size = atoi(argv[1]);
    printf("node size=%d starts...\n", size);
//    std::map<int, std::vector<int>> tree{{0, {1}},{1, {2}},{2, {3}}};
    std::vector<std::pair<salticidae::NetAddr, std::unique_ptr<MyNet>>> nodes;
    MyNet::Config config;
    salticidae::EventContext ec;
    config.ping_period(2);
    config.max_msg_size(10000000);
    nodes.resize(size);
    std::map<int, salticidae::PeerId> peerIdMap;

    for (size_t i = 0; i < nodes.size(); i++)
    {
        salticidae::NetAddr addr("127.0.0.1:" + std::to_string(10000 + i));
        auto &net = (nodes[i] = std::make_pair(addr,  std::make_unique<MyNet>(ec, config, i, std::to_string(i)))).second;
        salticidae::PeerId pid{addr};
        peerIdMap[i] = pid;
        std::cout << "addr port:" << addr.port << std::endl;
        net->start();
        net->listen(addr);
    }

    for (size_t i = 0; i < nodes.size(); i++)
        for (size_t j = 0; j < nodes.size(); j++)
            if (i != j)
            {
                auto &node = nodes[i].second;
                auto &peer_addr = nodes[j].first;
                salticidae::PeerId pid{peer_addr};
                node->add_peer(pid, (int) j);
                node->set_peer_addr(pid, peer_addr);
                node->conn_peer(pid);
                usleep(10);
            }

    /* the main loop can be shutdown by ctrl-c or kill */
    auto shutdown = [&](int) {ec.stop();};
    salticidae::SigEvent ev_sigint(ec, shutdown);
    salticidae::SigEvent ev_sigterm(ec, shutdown);
    ev_sigint.add(SIGINT);
    ev_sigterm.add(SIGTERM);

    uint32_t mid = nodes[0].second->trigger();;
    salticidae::TimerEvent ev_timer(ec, [&nodes, &mid, &ec](auto && ...) {
        nodes[0].second->send_msg_by_block();
        nodes[0].second->send_msg_by_block();
    });
    ev_timer.add(5);

    salticidae::TimerEvent ev_timer2(ec, [&nodes, &mid, &ec](auto && ...) {
        nodes[0].second->send_msg_by_block();
        nodes[0].second->send_msg_by_block();
    });
    ev_timer2.add(5.1);

    salticidae::TimerEvent ev_timer3(ec, [&nodes, &mid](auto && ...) {
        nodes[0].second->send_msg_by_block();
        nodes[0].second->send_msg_by_block();
    });
    ev_timer3.add(5.2);

    salticidae::TimerEvent ev_timer_backup(ec, [&nodes, &mid](auto && ...) {
        nodes[0].second->trigger_backup(mid);
    });
    ev_timer_backup.add(5.2);

    ec.dispatch();
}
