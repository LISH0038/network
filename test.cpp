#include <salticidae/network.h>
#include <set>
#include <map>
#include <iostream>
#include <utility>

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
struct MsgHello {
    static const uint8_t opcode = 0x0;
    DataStream serialized;
    std::string name;
    std::string text;
    uint32_t mid;
    /** Defines how to serialize the msg. */
    MsgHello(const std::string &name,
             const std::string &text,
             uint32_t mid) {
        serialized << mid;
        serialized << htole((uint32_t)name.length());
        serialized << name << text;
    }
    /** Defines how to parse the msg. */
    MsgHello(DataStream &&s) {
        s >> mid;
        uint32_t len;
        s >> len;
        len = letoh(len);
        name = std::string((const char *)s.get_data_inplace(len), len);
        len = s.size();
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
    std::map<int, salticidae::PeerId> peerIdMap;
    // mid:  <list of nid>
    std::map<uint32_t, std::unordered_set<uint32_t>> ackSet;

    void on_receive_hello(MsgHello &&msg, const MyNet::conn_t &conn) {
        printf("[%s] %s says %s, mid = %d\n", name.c_str(), msg.name.c_str(), msg.text.c_str(), msg.mid);
        /* send acknowledgement */
        send_msg(MsgAck(msg.mid, id), conn);
        /* send relay msg */
        for (int tid = id*fanout+1; tid < (id +1)*fanout+1; ++tid) {
            if (peerIdMap.find(tid) != peerIdMap.end()) {
                const auto &peerId = peerIdMap[tid];
                printf("[%s]  relay msg to %d, peer_port =%d\n", name.c_str(), tid, get_peer_conn(peerId)->get_peer_addr().port);
                // send_msg(msg, peerId); ?
                send_msg(MsgHello(name, "This is a relay broadcast!", msg.mid), peerId);
            }
        }

        /* go through back up children */
//        bool exit = false;
//        int count = 0;
//        while (!exit && count < 3) {
//            exit = true;
//            for (int tid = (id +1)*fanout+1; tid < peerIdMap.size()+1; ++tid) {
//                if (peerIdMap.find(tid) != peerIdMap.end() && ackSet.find(tid) == ackSet.end()) {
//                    printf("[%s] send to backup child %d\n", name.c_str(), tid);
//                    send_msg(MsgHello(name, "This is a back up broadcast!"), peerIdMap[tid]);
//                }
//                exit = false;
//                std::this_thread::sleep_for(std::chrono::milliseconds(20));
//            }
//            count ++;
//        }
    }

    void on_receive_ack(MsgAck &&msg, const MyNet::conn_t &conn) {
        auto net = static_cast<MyNet *>(conn->get_net());
        printf("[%s] receive ack from %d\n", net->name.c_str(), msg.nid);
        ackSet[msg.mid].insert(msg.nid);
        /* notify parent */
        if (id != 0) {
            int pid = id/fanout;
            const auto &peerId = peerIdMap[pid];
            send_msg(MsgAck(msg.mid, msg.nid), peerId);
        }
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
        uint32_t mid = rand();
        printf("[%s] let's do a broadcast!\n", name.c_str());
        for (const auto &peerId : peerIdMap) {
            printf("[%s]   send to %d\n", name.c_str(), peerId.first);
            send_msg(MsgHello(name, "This is a flat broadcast!", mid), peerId.second);
        }
        return mid;
    }

    void trigger_backup(uint32_t mid) {
        printf("[%s] trigger backup!\n", name.c_str());
        if (ackSet.find(mid) != ackSet.end()) {
            std::unordered_set<uint32_t> acks = ackSet[mid];
            for (const auto &peerId : peerIdMap) {
                if (acks.find(peerId.first) == acks.end()) {
                    printf("[%s] send to backup child %d\n", name.c_str(), peerId.first);
                    send_msg(MsgHello(name, "This is a back up broadcast!", mid), peerId.second);
                }
            } // to clean old msg
        } // else, keep waiting or send to all again?
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

    salticidae::TimerEvent ev_timer(ec, [&nodes](auto && ...) {
        uint32_t mid = nodes[0].second->trigger();
        sleep(1);// suspend whole program ??
        nodes[0].second->trigger_backup(mid);
    });
    for(int i = 0; i < 5; i++) {
        ev_timer.add(0.5*i);
    }

    ec.dispatch();
}
