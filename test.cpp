#include <salticidae/network.h>
#include <set>
#include <map>
#include <iostream>

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
    /** Defines how to serialize the msg. */
    MsgHello(const std::string &name,
             const std::string &text) {
        serialized << htole((uint32_t)name.length());
        serialized << name << text;
    }
    /** Defines how to parse the msg. */
    MsgHello(DataStream &&s) {
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
    MsgAck() {}
    MsgAck(DataStream &&s) {}
};

class MyNet: public Net {
//    std::map<int, salticidae::PeerId> peerIdMap;

    void on_receive_hello(MsgHello &&msg, const MyNet::conn_t &conn) {
        printf("[%s] %s says %s\n", name.c_str(), msg.name.c_str(), msg.text.c_str());
        /* send acknowledgement */
        send_msg(MsgAck(), conn);
    }

public:
    MyNet(const salticidae::EventContext &ec,
          const MyNet::Config &config,
          const std::string& name):
            Net(ec, config),
            name(name) {
        /* message handler could be a bound method */
        reg_handler(
                salticidae::generic_bind(&MyNet::on_receive_hello, this, _1, _2));

        reg_conn_handler([this](const ConnPool::conn_t &conn, bool connected) {
            if (connected)
            {
                if (conn->get_mode() == ConnPool::Conn::ACTIVE)
                {
                    printf("[%s] connected.\n",
                           this->name.c_str());
//                    /* send the first message through this connection */
//                    send_msg(MsgHello(this->name, "Hello there!"),
//                             salticidae::static_pointer_cast<Conn>(conn));
                }
                else
                    printf("[%s] accepted, waiting for greetings.\n",
                           this->name.c_str());
            }
            else
            {
                printf("[%s] disconnected, retrying.\n", this->name.c_str());
                /* try to reconnect to the same address */
//                connect(obj->get_addr());
                auto obj = salticidae::static_pointer_cast<conn_t::type>(conn);
                conn_peer(get_peer_id(obj, obj->get_addr()));
            }
            return true;
        });
    }

//    std::set<PeerId> childPeers;
//    std::unordered_set<uint256_t> valid_tls_certs;
//    std::vector<PeerId> peers;
//    /** network stack */
//
    const std::string name;
};

void on_receive_ack(MsgAck &&msg, const MyNet::conn_t &conn) {
    auto net = static_cast<MyNet *>(conn->get_net());
    printf("[%s] the peer knows\n", net->name.c_str());
}

int main() {
//    std::map<int, std::vector<int>> tree{{0, {1,2}},{1, {3,4}},{2, {5,6}}};
    std::map<int, std::vector<int>> tree{{0, {1}},{1, {2}},{2, {3}}};
    std::vector<std::pair<salticidae::NetAddr, std::unique_ptr<MyNet>>> nodes;
    MyNet::Config config;
    salticidae::EventContext ec;
    config.ping_period(2);
    nodes.resize(4);
    std::map<int, salticidae::PeerId> peerIdMap;

    for (size_t i = 0; i < nodes.size(); i++)
    {
        salticidae::NetAddr addr("127.0.0.1:" + std::to_string(10000 + i));
        auto &net = (nodes[i] = std::make_pair(addr,  std::make_unique<MyNet>(ec, config, std::to_string(i)))).second;
        salticidae::PeerId pid{addr};
        peerIdMap[i] = pid;
        std::cout << "addr port:" << addr.port << std::endl;
        net->reg_handler(on_receive_ack);
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
                node->add_peer(pid);
                node->set_peer_addr(pid, peer_addr);
                node->conn_peer(pid);
                usleep(10);
            }
//    usleep(1000);
    for (size_t i = 0; i < nodes.size(); i++) {
//        printf("here:");
        std::thread thread_object([&i, &nodes, &tree, &peerIdMap] {
            if (tree.find(i) != tree.end()) {
                printf("here:");
                for (int k =0; k < tree[i].size(); k++) {
                    printf("%d",tree[i][k]);
                }
//                sleep(1);
                std::vector<salticidae::PeerId> children;
                std::transform(
                        tree[i].begin(), tree[i].end(), children.begin(), [&peerIdMap](int a) -> PeerId { return peerIdMap[a];});
                int count = 0;
                while (count < 30) {
//                    printf("here:");
//                    for (int k =0; k < children.size(); k++) {
//                        printf("%s",children[k].to_hex().c_str());
//                    }
                    count ++;
                    sleep(1);
                    nodes[i].second->multicast_msg(MsgHello(std::to_string(i), "Hello there! count = " + std::to_string(count)), children);
                };
            }
        });
    }

    /* the main loop can be shutdown by ctrl-c or kill */
    auto shutdown = [&](int) {ec.stop();};
    salticidae::SigEvent ev_sigint(ec, shutdown);
    salticidae::SigEvent ev_sigterm(ec, shutdown);
    ev_sigint.add(SIGINT);
    ev_sigterm.add(SIGTERM);

    ec.dispatch();
}
