#include "QSDCRepeatersApplication.h"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>

#include <omnetpp.h>

#include "messages/classical_messages.h"
#include "messages/qsdc_messages_m.h"
#include "modules/QNIC/StationaryQubit/StationaryQubit.h"


// Tim's code to logging
namespace {
#define QLOG(expr)                         \
    do {                                   \
        std::ostringstream _qs;            \
        _qs << expr;                       \
        EV_INFO << _qs.str() << "\n";      \
        /* Keep file logging too if desired */ \
        std::ofstream logfile("qsdc_app.log", std::ios::app); \
        if (logfile.is_open()) {           \
            logfile << std::fixed << std::setprecision(9) \
                    << "[" << omnetpp::simTime() << "] " \
                    << _qs.str() << "\n";  \
            logfile.flush();               \
        }                                  \
    } while (0)
}



using namespace omnetpp;
using namespace quisp::messages;

namespace quisp::modules {

Define_Module(QSDCRepeatersApplication);

// Custom message values to define each node's behaviors in the protocol

static const char* SELF_GENERATE_PAIRS = "SELF_GENERATE_PAIRS";
static const char* SELF_WAIT_FOR_PAIRS = "SELF_WAIT_FOR_PAIRS";
static const char* SELF_RECEPTION_TIMEOUT = "SELF_RECEPTION_TIMEOUT";
static const char* SELF_SEND_PHOTON = "SELF_SEND_PHOTON";
static const char* ACK_RECEIVED_PHOTON = "ACK_RECEIVED_PHOTON";

// Protocol messages
static const char* SELF_QSDC_PREPARE = "SELF_QSDC_PREPARE";
static const char* QSDC_COMM_START = "QSDC_COMM_START";
static const char* QSDC_COMM_READY = "QSDC_COMM_READY";
static const char* QSDC_COMM_SYNC = "QSDC_COMM_SYNC";
static const char* QSDC_QUBIT_RECEIVED = "QSDC_QUBIT_RECEIVED";
static const char* QSDC_COMM_END = "QSDC_COMM_END";



void QSDCRepeatersApplication::initialize() {
    initializeLogger(provider);

    // Only keep this module for EndNodes (same logic as Application.cc)
    // Basically, only end nodes should run this.

    // I decided to use this module in repeater modules in the first instance, so this part might be useless

    // if (!gate("toRouter")->isConnected() || !gate("fromRouter")->isConnected()) {
    //   auto* msg = new DeleteThisModule("DeleteThisModule");
    //   scheduleAt(simTime(), msg);
    //   return;
    // }

    my_address = provider.getNodeAddr();

    auto* qnode = provider.getQNode();
    if (!qnode) {
        QLOG("[QSDC] No QNode found in initialize()");
        return;
    }

    // basic parameters to assign the roles of each node during communication
    // 4 roles: Alice and Bob (who want to communicate through QSDC)
    //          Server (who generates the entangled pairs on Psi-)
    //          Repeaters (who pass the messages with entanglement swapping)

    is_alice            = par("is_alice").boolValue();
    is_bob              = par("is_bob").boolValue();
    is_repeater         = par("is_repeater").boolValue();
    is_server           = par("is_server").boolValue();
    // test variable to make the testing process easier
    is_test     = par("is_test").boolValue();

    
    // Read NED parameters
    if (hasPar("total_qubits_to_send")) {
        total_qubits_to_send = par("total_qubits_to_send").intValue();
    } else {
        total_qubits_to_send = 10; // Default fallback
    }

    if (is_server) {
        // Trigger the FSM instead of the old 'is_test' blind send
        scheduleAt(simTime(), new cMessage(SELF_QSDC_PREPARE));
    }
}

void QSDCRepeatersApplication::sendNextQubitPair() {
    QLOG("[SERVER] Emitting Qubit Pair Index: " << current_qubit_index);
    
    // Reset flags for the next round
    alice_received_current = false;
    bob_received_current = false;

    auto qubit_pair = generateEntangledPairs(1, "qnic", 1, BellState::PsiMinus);
    if(qubit_pair.empty()) {
        QLOG("[SERVER] ERROR: Quantum Memory Exhaustion. Cannot generate pair.");
        return; 
    }

    auto* photon_left = new quisp::messages::PhotonicQubit("SERVER_PHOTON_L");
    photon_left->setQubitRef(qubit_pair[0].qubit_1->getBackendQubitRef());
    photon_left->addPar("sequence_number") = current_qubit_index;
    photon_left->addPar("direction") = "left";

    auto* photon_right = new quisp::messages::PhotonicQubit("SERVER_PHOTON_R");
    photon_right->setQubitRef(qubit_pair[0].qubit_2->getBackendQubitRef());
    photon_right->addPar("sequence_number") = current_qubit_index;
    photon_right->addPar("direction") = "right";

    send(photon_left, "toQuantum_l");
    send(photon_right, "toQuantum_r");
}

void QSDCRepeatersApplication::sendClassicalMessage(int dest_addr, const char* msg_type, const char* msg_name, int seq_num) {
    auto* pkt = new QSDCSynAck(msg_name);
    pkt->setSrcAddr(my_address);
    pkt->setDestAddr(dest_addr);
    pkt->setName(msg_type);
    
    if (is_alice) pkt->addPar("from") = "alice";
    else if (is_bob) pkt->addPar("from") = "bob";
    else if (is_server) pkt->addPar("from") = "server";
    
    if (seq_num != -1) {
        pkt->addPar("index") = seq_num;
    }
    QLOG("[MESSAGE] Sending ClassicalMessage " << msg_name << " to " << ((dest_addr == 0) ? "Alice" : (dest_addr == 4) ? "Bob" : "Server"));
    
    send(pkt, "toRouter");
}




void QSDCRepeatersApplication::protocolInit() {
    // override the standard connection manager for testing the physical layer swap
    if (is_initiator) {
        QLOG("[QSDC Alice] Bypassing Connection Manager for direct Server test.");
        // delay long enough for Server to generate, Repeater to swap, and Bob to apply corrections
        scheduleAt(simTime() + start_delay + par("sample_interval"), new cMessage(SELF_WAIT_FOR_PAIRS));
    }
}

/* generateEntangledPairs 
*
*   generates in a QNIC the desired bell state to be sent or operated with during the protocol
*
*/
std::vector<LocalBellPair> QSDCRepeatersApplication::generateEntangledPairs(int n, const char* qnic_type, int qnic_index, BellState state) {
    QLOG("[QSDC PairGen] Generating Entangled Pairs...");
    std::vector<LocalBellPair> generated_pairs;
    
    if (n <= 0) return generated_pairs;

    auto* qnic = getQNIC(qnic_type, qnic_index);
    if (!qnic) {
        QLOG("[QSDC PairGen] ERROR: Target QNIC " << qnic_type << "[" << qnic_index << "] not found.");
        return generated_pairs;
    }

    const int num_buf = qnic->par("num_buffer").intValue();
    std::vector<int> free_indices;

    for (int i = 0; i < num_buf; i++) {
        auto* sq_mod = qnic->getSubmodule("statQubit", i);
        if (!sq_mod) continue;

        auto* sq = check_and_cast<quisp::modules::StationaryQubit*>(sq_mod);
        
        if (!sq->isBusy() && !sq->isLocked()) {
            free_indices.push_back(i); 
        }
        
        if (free_indices.size() == 2 * n) {
            break; // We have identified enough memory slots
        }
    }

    // atomic allocation check
    if (free_indices.size() < 2 * n) {
        QLOG("[QSDC PairGen] Insufficient memory. Requested " << n << " pairs (" << 2*n << " qubits), but only " << free_indices.size() << " available in QNIC.");
        return generated_pairs; // caller must handle resource exhaustion
    }

    // lock hardware and apply quantum logic gates
    for (int i = 0; i < n; i++) {
        int qi_1 = free_indices[2 * i];
        int qi_2 = free_indices[2 * i + 1];

        auto* qubit_1 = check_and_cast<quisp::modules::StationaryQubit*>(qnic->getSubmodule("statQubit", qi_1));
        auto* qubit_2 = check_and_cast<quisp::modules::StationaryQubit*>(qnic->getSubmodule("statQubit", qi_2));

        // lock the qubits from the hardware layer upwards
        qubit_1->setBusy();
        qubit_2->setBusy();

        // maximum correlation base generation: |00> + |11> (Phi+)
        qubit_1->gateHadamard();
        qubit_1->gateCNOT(qubit_2);

        if (state == BellState::PhiPlus) {
            QLOG("[QSDC PairGen] Generating local Bell pair across qi=" << qi_1 << " and qi=" << qi_2  << " mapped to state: Phi+");  
            // |00> + |11> (Base state, no operations needed)
        } else if (state == BellState::PhiMinus) { 
            QLOG("[QSDC PairGen] Generating local Bell pair across qi=" << qi_1 << " and qi=" << qi_2  << " mapped to state: Phi-");  
            // |00> - |11>
            qubit_1->gateZ(); 
        } else if (state == BellState::PsiPlus) {  
            QLOG("[QSDC PairGen] Generating local Bell pair across qi=" << qi_1 << " and qi=" << qi_2 << " mapped to state: Psi+");  
            // |01> + |10>
            qubit_2->gateX(); 
        } else if (state == BellState::PsiMinus) { // Actually used on the protocol
            QLOG("[QSDC PairGen] Generating local Bell pair across qi=" << qi_1 << " and qi=" << qi_2 << " mapped to state: Psi-");  
            // |01> - |10>
            qubit_1->gateZ();
            qubit_2->gateX();
        }

        // Pack the physical references into the return struct
        generated_pairs.push_back({qnic_index, qi_1, qi_2, qubit_1, qubit_2});

    }

    return generated_pairs;
}

/* measureBellStateAndSend
*   grabs 2 bits and applies CNOT and Hadammard gates to perform the ES
*   It will be used on the "Repeater" function
*
*/

void QSDCRepeatersApplication::measureBellStateAndSend(quisp::backends::IQubit* incoming_qubit, quisp::modules::StationaryQubit* local_qubit, int dst_addr, int seq_num) {
    if (!incoming_qubit || !local_qubit) {
        QLOG("[QSDC BSM] ERROR: Null qubit references passed to measureBellStateAndSend.");
        return;
    }

    // entanglement Swapping gates
    incoming_qubit->gateCNOT(local_qubit->getBackendQubitRef());
    incoming_qubit->gateH();

    int z0_result = eigenToInt(incoming_qubit->measureZ());
    int z1_result = eigenToInt(local_qubit->measureZ());


    int bsm_outcome = 0;
    if (z0_result == +1 && z1_result == +1)      {
        QLOG("[QSDC BSM] Entanglement Swapping successful. Outcome: Phi+");
        bsm_outcome = 0; 
    }      // Phi+
    else if (z0_result == -1 && z1_result == +1) {
        QLOG("[QSDC BSM] Entanglement Swapping successful. Outcome: Phi-");
        bsm_outcome = 1; 
    } // Phi-
    else if (z0_result == +1 && z1_result == -1) {
        QLOG("[QSDC BSM] Entanglement Swapping successful. Outcome: Psi+");
        bsm_outcome = 2; 
    } // Psi+
    else if (z0_result == -1 && z1_result == -1) {
        QLOG("[QSDC BSM] Entanglement Swapping successful. Outcome: Psi-");
        bsm_outcome = 3; 
    } // Psi-


    local_qubit->Unlock(); 

    // send classical Pauli-frame correction data
    if(dst_addr != -1) {
        QSDCBSMResult* bsm_packet = new QSDCBSMResult("BSM_Announcement");
        bsm_packet->setSrcAddr(my_address);
        bsm_packet->setDestAddr(dst_addr); // Fixed variable name here
        bsm_packet->setBsmOutcome(bsm_outcome);
        bsm_packet->setSequenceNum(seq_num);
        send(bsm_packet, "toRouter");
    }
}

/*  QNIC = Quantum Network Interface Card :
*
*   Alice and Bob both will use QNIC as a Receiver (qnic_r)
*   Server will also use the QNIC as a Emitter (qnic)
*   Repeater will use both
*
*/  
omnetpp::cModule* QSDCRepeatersApplication::getQNIC(const char* qnic_type, int qnic_index) {
    auto* qnode = provider.getQNode();
    if (!qnode) return nullptr;

    // qnic_type will be "qnic", "qnic_r", or "qnic_rp"
    return qnode->getSubmodule(qnic_type, qnic_index);

    if (auto* m = qnode->getSubmodule("qnic_rp", 0)) return m;
    if (auto* m = qnode->getSubmodule("qnic_r", 0)) return m;
    if (auto* m = qnode->getSubmodule("qnic", 0)) return m;

    return nullptr;
}

void QSDCRepeatersApplication::handleIncomingPhotonAtEndNode(quisp::messages::PhotonicQubit* photon) {
    int seq_num = (int)photon->par("sequence_number").longValue();
    QLOG("[ENDNODE] Received PhotonicQubit sequence: " << seq_num);
    
    if (is_alice || is_bob) {
        // extract the backend state tracking reference
        auto* backend_qubit = const_cast<quisp::backends::IQubit*>(photon->getQubitRef());
        
        // store in local buffer
        received_qubits.push_back(backend_qubit);
        
        // acknowledge receipt to the Server
        sendClassicalMessage(2, QSDC_QUBIT_RECEIVED, "QSDC_QUBIT_RECEIVED", seq_num); // 2 is server
    }
    
    // delete OMNeT++ envelope, backend quantum state is preserved in received_qubits
    delete photon; 
}

void QSDCRepeatersApplication::handleIncomingPhotonAtRepeater(quisp::messages::PhotonicQubit* photon) {
    QLOG("[TEST] Processing Photon to perform the Entanglement Swap...");
    // step 1: create a new pair of entangled qubits on the psi- state
    auto new_pairs = generateEntangledPairs(1, "qnic", 1, BellState::PsiMinus);
    int seq_num = (int)photon->par("sequence_number").longValue();
    std::string photon_direction = photon->par("direction").stringValue();

    if (new_pairs.empty()) {
        QLOG("[REPEATER] FATAL: Insufficient quantum memory to generate ES pair. Dropping photon.");
        delete photon;
        return;
    }

    auto* local_half = new_pairs[0].qubit_1;  
    auto* remote_half = new_pairs[0].qubit_2; 

    // extract the incoming backend IQubit from the OMNeT++ message
    auto* incoming_qubit = const_cast<quisp::backends::IQubit*>(photon->getQubitRef());


    // If the photon is flowing "left" (towards Alice), the repeater sends BSM results to Alice.
    // If the photon is flowing "right" (towards Bob), the repeater sends BSM results to Bob.
    int dst_addr = (photon_direction == "left") ? par("alice_address").intValue() : par("bob_address").intValue();



    // step 2: entanglement swap with the incoming bit 
    measureBellStateAndSend(incoming_qubit, local_half, dst_addr, seq_num);

    local_half->setFree(true);

    auto* next_photon = new quisp::messages::PhotonicQubit("FORWARDED_PHOTON");
    next_photon->setQubitRef(remote_half->getBackendQubitRef());
    next_photon->addPar("src_addr") = my_address;
    next_photon->addPar("qubit_index") = new_pairs[0].qi_2;
    next_photon->addPar("sequence_number") = seq_num;

    if (photon_direction == "left") send(next_photon, "toQuantum_l");
    if (photon_direction == "right") send(next_photon, "toQuantum_r");
    
    delete photon;
}


// Utility mapper for eigenvalues
int QSDCRepeatersApplication::eigenToInt(quisp::backends::abstract::EigenvalueResult r) {
    return (r == quisp::backends::abstract::EigenvalueResult::PLUS_ONE) ? +1 : -1;
}


void QSDCRepeatersApplication::handleMessage(cMessage *msg) {
    QLOG("[MESSAGE] New message received in " << (is_alice ? "Alice" : is_bob ? "Bob" : "Server"));
    if (auto* photon = dynamic_cast<quisp::messages::PhotonicQubit*>(msg)) {
        if (is_repeater) {
            QLOG("[TEST] Photons arrived at Repeater...");
            handleIncomingPhotonAtRepeater(photon);
        } else if (is_alice || is_bob) {
            QLOG("[TEST] Photons arrived at Repeater...");
            handleIncomingPhotonAtEndNode(photon);
        } else {
            QLOG("[QSDC PHOTON] Photon received on a non-repeater and non-terminal node");
            delete photon;
        }
        return;
    }

    // After measuring, the endNode should make its own polarizations to make stay with the desired entanglement
    if (dynamic_cast<QSDCBSMResult *>(msg)) {
        auto* bsm_msg = check_and_cast<QSDCBSMResult*>(msg);
        int seq_num = bsm_msg->getSequenceNum();
        int bsm_outcome = bsm_msg->getBsmOutcome();
        delete msg;
        return;
    }

    // FSM messages
    if (strcmp(msg->getName(), SELF_QSDC_PREPARE) == 0) {
        QLOG("[SERVER] Initializing QSDC Protocol. Requesting EndNode Readiness.");
        sendClassicalMessage(0, QSDC_COMM_START, "QSDC_COMM_START"); //0 is alice 4 is bob
        sendClassicalMessage(4, QSDC_COMM_START, "QSDC_COMM_START");
        delete msg;
        return;
    }

    if (strcmp(msg->getName(), QSDC_COMM_START) == 0) {
        QLOG("[TEST] Incomming QSDC_COMM_START");
        if (is_repeater) {
            QLOG("[REPEATER] Received a Classical message to repeat.");
            send(msg, "toRouter");
            delete(msg);
            return;
        }

        QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] Received Start. Sending Ready.");
        sendClassicalMessage(2, QSDC_COMM_READY, "QSDC_COMM_READY"); //2 is server
        delete(msg);
        return;
    }


    if (auto* pkt = dynamic_cast<quisp::messages::QSDCSynAck*>(msg)) {

        if (is_repeater) {
            QLOG("[REPEATER] Received a Classical message to repeat.");
            send(msg, "toRouter");
            delete(msg);
            return;
        }

        std::string msg_type = pkt->getName();
        std::string from = pkt->par("from").stringValue();


         if (msg_type == QSDC_COMM_SYNC) {
            QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] Received Sync. Expecting qubits.");
            received_qubits.clear(); // Clear buffer for new session
        }
        else if (msg_type == QSDC_COMM_END) {
            QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] Received End. Protocol Complete. Buffered qubits: " << received_qubits.size());
        }

        // Server Logic
        else if (is_server && msg_type == QSDC_COMM_READY) {
            if (from == "alice") alice_ready = true;
            if (from == "bob") bob_ready = true;

            if (alice_ready && bob_ready) {
                QLOG("[SERVER] Both nodes ready. Sending Sync and starting Qubit 0.");
                sendClassicalMessage(0, QSDC_COMM_SYNC, "QSDC_COMM_SYNC"); // 0 is alice 4 is bob
                sendClassicalMessage(4, QSDC_COMM_SYNC, "QSDC_COMM_SYNC");
                current_qubit_index = 0;
                sendNextQubitPair();
            }
        }
        else if (is_server && msg_type == QSDC_QUBIT_RECEIVED) {
            int rcv_index = (int)pkt->par("index").longValue();
            
            // Ensure we are tracking the correct index
            if (rcv_index == current_qubit_index) {
                if (from == "alice") alice_received_current = true;
                if (from == "bob") bob_received_current = true;

                if (alice_received_current && bob_received_current) {
                    QLOG("[SERVER] Qubit " << current_qubit_index << " synchronized at both ends.");

                    current_qubit_index++;

                    if (current_qubit_index < total_qubits_to_send) {
                        sendNextQubitPair();
                    } else {
                        QLOG("[SERVER] Transmission Complete. Sending END signal.");
                        sendClassicalMessage(0, QSDC_COMM_END, "Comm_End"); // 0 is alice 4 is bob
                        sendClassicalMessage(4, QSDC_COMM_END, "Comm_End");
                    }
                }
            } else {
                QLOG("[SERVER] Received out-of-sync ACK index: " << rcv_index);
            }
        }

        delete msg;
        return;
    }

    if (strcmp(msg->getName(), SELF_SEND_PHOTON) == 0) {
        QLOG("[TEST] Generating Psi- Entangled Pairs and sending them to both connections");
        auto qubit_pair = generateEntangledPairs(1, "qnic", 1, BellState::PsiMinus);

        auto* left_half = qubit_pair[0].qubit_1;  
        auto* right_half = qubit_pair[0].qubit_2; 

        auto* photon_left = new quisp::messages::PhotonicQubit("TEST_SERVER_LEFT_PHOTON");
        photon_left->setQubitRef(left_half->getBackendQubitRef());

        auto* photon_right = new quisp::messages::PhotonicQubit("TEST_SERVER_RIGHT_PHOTON");
        photon_right->setQubitRef(right_half->getBackendQubitRef());

        
        photon_left->addPar("sequence_number") = 0;
        photon_left->addPar("direction") = "left";

        photon_right->addPar("sequence_number") = 1;
        photon_right->addPar("direction") = "right";

        send(photon_left, "toQuantum_l");
        send(photon_right, "toQuantum_r");
        delete msg;
        return;
    }

    if (auto* resp = dynamic_cast<quisp::messages::QSDCSynAck*>(msg)) {
        QLOG("[TEST] EndNode received QuBIT");
        // when Alice and bob receive a qubit, they send it to Server with the corresponding index to say that they received
        // check index, if not good, they send a rollback bit (Alice didn't receive qubit n, but received qubit n+1)
        // so they send qubit correction (and after that, they resend qubit n to both sides)
        // must have a timeout

        std::string from = resp->par("from").stringValue();
        if (from == "alice") QLOG("[SERVER] Alice received QuBIT");
        if (from == "bob")  QLOG("[SERVER] Bob received QuBIT");
        delete msg;
        return;
    }

    if (strcmp(msg->getName(), "QUBIT_CORRECTION") == 0) {
        QLOG("[SERVER] Correction of not-received QuBIT");
        // if is server etc has to do it, else it propagates
        // when Alice and bob receive a qubit, they send it to Server
        
        delete msg;
        return;
    }
    return;
}



}  // namespace quisp::modules

