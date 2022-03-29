#include <cstdio>
#include <string>
#include <functional>
#include <set>
#include "salticidae/event.h"
#include "salticidae/network.h"
#include "salticidae/stream.h"

using salticidae::NetAddr;
using salticidae::DataStream;
using salticidae::MsgNetwork;
using salticidae::htole;
using salticidae::letoh;
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

const uint8_t MsgHello::opcode;
const uint8_t MsgAck::opcode;

using MsgNetworkByteOp = MsgNetwork<uint8_t>;

struct MyNet: public MsgNetworkByteOp {
    const std::string name;
    const NetAddr peer;

    MyNet(const salticidae::EventContext &ec,
          const std::string name,
          const NetAddr &peer):
            MsgNetwork<uint8_t>(ec, MsgNetwork::Config()),
            name(name),
            peer(peer) {
        /* message handler could be a bound method */
        reg_handler(
                salticidae::generic_bind(&MyNet::on_receive_hello, this, _1, _2));

        reg_conn_handler([this](const ConnPool::conn_t &conn, bool connected) {
            if (connected)
            {
                if (conn->get_mode() == ConnPool::Conn::ACTIVE)
                {
                    printf("[%s] connected, sending hello.\n",
                           this->name.c_str());
                    /* send the first message through this connection */
                    send_msg(MsgHello(this->name, "Hello there!"),
                             salticidae::static_pointer_cast<Conn>(conn));
                }
                else
                    printf("[%s] accepted, waiting for greetings.\n",
                           this->name.c_str());
            }
            else
            {
                printf("[%s] disconnected, retrying.\n", this->name.c_str());
                /* try to reconnect to the same address */
                connect(conn->get_addr());
            }
            return true;
        });
    }

    void on_receive_hello(MsgHello &&msg, const MyNet::conn_t &conn) {
        printf("[%s] %s says %s\n", name.c_str(), msg.name.c_str(), msg.text.c_str());
        /* send acknowledgement */
        send_msg(MsgAck(), conn);
    }
};

void on_receive_ack(MsgAck &&msg, const MyNet::conn_t &conn) {
    auto net = static_cast<MyNet *>(conn->get_net());
    printf("[%s] the peer knows\n", net->name.c_str());
}

int main() {
    salticidae::EventContext ec;
    NetAddr alice_addr("127.0.0.1:12345");
    NetAddr bob_addr("127.0.0.1:12346");

    /* test two nodes in the same main loop */
    MyNet alice(ec, "alice", bob_addr);
    MyNet bob(ec, "bob", alice_addr);

    /* message handler could be a normal function */
    alice.reg_handler(on_receive_ack);
    bob.reg_handler(on_receive_ack);

    /* start all threads */git
    alice.start();
    bob.start();

    /* accept incoming connections */
    alice.listen(alice_addr);
    bob.listen(bob_addr);

    /* try to connect once */
    alice.connect(bob_addr);
    bob.connect(alice_addr);

    /* the main loop can be shutdown by ctrl-c or kill */
    auto shutdown = [&](int) {ec.stop();};
    salticidae::SigEvent ev_sigint(ec, shutdown);
    salticidae::SigEvent ev_sigterm(ec, shutdown);
    ev_sigint.add(SIGINT);
    ev_sigterm.add(SIGTERM);

    /* enter the main loop */
    ec.dispatch();
}
