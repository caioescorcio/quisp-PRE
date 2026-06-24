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
static const char* QSDC_COMM_ACK = "QSDC_COMM_ACK";
static const char* QSDC_COMM_END = "QSDC_COMM_END";


static const char* QSDC_QUBIT_SYNC = "QSDC_QUBIT_SYNC";
static const char* QSDC_QUBIT_ACK = "QSDC_QUBIT_ACK";
static const char* QSDC_QUBIT_CONTINUE = "QSDC_QUBIT_CONTINUE";
static const char* QSDC_QUBIT_ERROR = "QSDC_QUBIT_ERROR";
static const char* QSDC_QUBIT_DISCARD = "QSDC_QUBIT_DISCARD";




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
        total_qubits_to_send = 20; // Default fallback
    }

    if (is_server) {
        // Trigger the FSM instead of the old 'is_test' blind send
        scheduleAt(simTime(), new cMessage(SELF_QSDC_PREPARE));
    }
}

void QSDCRepeatersApplication::sendNextQubitPair() {
    QLOG("[SERVER] Emitting Purification Batch: Target Qubit " << current_qubit_index << " & Source Qubit " << current_qubit_index + 1);
    
    // Reset flags for the next round
    alice_received_current = false;
    bob_received_current = false;
    alice_continue_ready = false;
    bob_continue_ready = false;
    server_is_rolling_back = false; 

    // Generate TWO pairs instead of one
    auto qubit_pairs = generateEntangledPairs(2, "qnic", 1, BellState::PsiMinus);
    if(qubit_pairs.size() < 2) {
        QLOG("[SERVER] ERROR: Quantum Memory Exhaustion. Cannot generate 2 pairs.");
        return; 
    }

    // 1. Send Target Qubit (Index K)
    sendClassicalMessage(0, QSDC_QUBIT_SYNC, "QSDC_QUBIT_SYNC", current_qubit_index); 
    sendClassicalMessage(4, QSDC_QUBIT_SYNC, "QSDC_QUBIT_SYNC", current_qubit_index);

    auto* photon_left_0 = new quisp::messages::PhotonicQubit("SERVER_PHOTON_L_TARGET");
    photon_left_0->setQubitRef(qubit_pairs[0].qubit_1->getBackendQubitRef());
    photon_left_0->addPar("sequence_number") = current_qubit_index;
    photon_left_0->addPar("direction") = "left";

    auto* photon_right_0 = new quisp::messages::PhotonicQubit("SERVER_PHOTON_R_TARGET");
    photon_right_0->setQubitRef(qubit_pairs[0].qubit_2->getBackendQubitRef());
    photon_right_0->addPar("sequence_number") = current_qubit_index;
    photon_right_0->addPar("direction") = "right";

    send(photon_left_0, "toQuantum_l");
    send(photon_right_0, "toQuantum_r");

    // 2. Send Source Qubit (Index K + 1)
    sendClassicalMessage(0, QSDC_QUBIT_SYNC, "QSDC_QUBIT_SYNC", current_qubit_index + 1); 
    sendClassicalMessage(4, QSDC_QUBIT_SYNC, "QSDC_QUBIT_SYNC", current_qubit_index + 1);

    auto* photon_left_1 = new quisp::messages::PhotonicQubit("SERVER_PHOTON_L_SOURCE");
    photon_left_1->setQubitRef(qubit_pairs[1].qubit_1->getBackendQubitRef());
    photon_left_1->addPar("sequence_number") = current_qubit_index + 1;
    photon_left_1->addPar("direction") = "left";

    auto* photon_right_1 = new quisp::messages::PhotonicQubit("SERVER_PHOTON_R_SOURCE");
    photon_right_1->setQubitRef(qubit_pairs[1].qubit_2->getBackendQubitRef());
    photon_right_1->addPar("sequence_number") = current_qubit_index + 1;
    photon_right_1->addPar("direction") = "right";

    send(photon_left_1, "toQuantum_l");
    send(photon_right_1, "toQuantum_r");
}
void QSDCRepeatersApplication::sendClassicalMessage(int dest_addr, const char* msg_type, const char* msg_name, int seq_num, int meas_res) {
    auto* pkt = new QSDCSynAck(msg_name);
    pkt->setSrcAddr(my_address);
    pkt->setDestAddr(dest_addr);
    pkt->setName(msg_type);
    
    if (is_alice) pkt->setFromNode("alice");
    else if (is_bob) pkt->setFromNode("bob");
    else if (is_server) pkt->setFromNode("server");
    else pkt->setFromNode("repeater");

    pkt->setSequenceNum(seq_num);

    pkt->setMeasResult(meas_res);
    
    QLOG("[MESSAGE] Sending ClassicalMessage " << msg_name << " to " << ((dest_addr == 0) ? "Alice" : (dest_addr == 4) ? "Bob" : "Server"));
    
    send(pkt, "toRouter");
}




void QSDCRepeatersApplication::protocolInit() {
    // override the standard connection manager for testing the physical layer swap
    if (is_initiator) {
        QLOG("[QSDC Alice] Bypassing Connection Manager for direct Server test.");
        // delay long enough for Server to generate, Repeater to swap, and Bob to apply corrections
        scheduleAt(simTime() + par("sample_interval"), new cMessage(SELF_WAIT_FOR_PAIRS));
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

void QSDCRepeatersApplication::handleBSMResult(int seq_num, int bsm_outcome) {
    if (received_qubits.find(seq_num) != received_qubits.end()) {
        auto* qubit = received_qubits[seq_num];
        
        QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] Applying BSM Correction to Qubit " << seq_num << " (Outcome: " << bsm_outcome << ")");
        
        if (bsm_outcome == 1) qubit->gateZ();       
        if (bsm_outcome == 2) qubit->gateX();       
        if (bsm_outcome == 3) {                     
            qubit->gateX(); 
            qubit->gateZ(); 
        }
        
        bsm_arrival_counts[seq_num]++;
        
        if (bsm_arrival_counts[seq_num] == par("expected_bsms").intValue()) {
            QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] All BSMs received for Qubit " << seq_num << ". Queuing for Purification.");
            ready_qubits.push_back(seq_num);
            attemptPurification(); 
        }
    } else {
        QLOG("[WARNING] BSM arrived before photon for sequence: " << seq_num);
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

void QSDCRepeatersApplication::attemptPurification() {
    if (ready_qubits.size() < 2) {
        QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] Only 1 qubit ready. Waiting for a pair to purify.");
        return;
    }

    int target_seq = ready_qubits[0];
    int source_seq = ready_qubits[1];

    ready_qubits.erase(ready_qubits.begin(), ready_qubits.begin() + 2);

    auto* target_qubit = received_qubits[target_seq];
    auto* source_qubit = received_qubits[source_seq];

    QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] Executing Purification. Target: " << target_seq << ", Source: " << source_seq);

    if (is_alice) {
        target_qubit->gateX(); target_qubit->gateZ();
        source_qubit->gateX(); source_qubit->gateZ();
    }

    target_qubit->gateCNOT(source_qubit);

    int meas_res = eigenToInt(source_qubit->measureZ());
    
    // CRITICAL FIX 1: Store the measurement safely in our map
    my_local_measurements[target_seq] = meas_res; 
    QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] Z-Measurement Result for Qubit " << target_seq << ": " << meas_res);

    // Free the source qubit (Assuming IQubit pointer, though StationaryQubit is safer in QuISP. Using true boolean)
    // NOTE: Depending on your exact QuISP backend, you might just erase it if setFree() throws an error.
    source_qubit->setFree(); 
    received_qubits.erase(source_seq); 

    int partner_address = is_alice ? 4 : 0; // Alice sends to Bob(4), Bob sends to Alice(0)
    sendClassicalMessage(partner_address, "QSDC_PURIFY_RESULT", "Purify_Result", target_seq, meas_res);
}

void QSDCRepeatersApplication::handleIncomingPhotonAtEndNode(quisp::messages::PhotonicQubit* photon) {
    int seq_num = (int)photon->par("sequence_number").longValue();
    QLOG("[ENDNODE] Received PhotonicQubit sequence: " << seq_num);
    
    if (is_alice || is_bob) {
        // extract the backend state tracking reference
        auto* backend_qubit = const_cast<quisp::backends::IQubit*>(photon->getQubitRef());
        
        // store in the asynchronous map
        received_qubits[seq_num] = backend_qubit;
        
        // We do NOT send ACK_RECEIVED_PHOTON here anymore.
        // We wait for the BSM packets to arrive, apply corrections, and then 
        // the attemptPurification / BSM logic handles the classical synchronization.
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
    if (auto* photon = dynamic_cast<quisp::messages::PhotonicQubit*>(msg)) {
        if (is_repeater) {
            handleIncomingPhotonAtRepeater(photon);
        } else if (is_alice || is_bob) {
            handleIncomingPhotonAtEndNode(photon);
        } else {
            delete photon;
        }
        return;
    }

    if (dynamic_cast<QSDCBSMResult *>(msg)) {
        auto* bsm_msg = check_and_cast<QSDCBSMResult*>(msg);
        handleBSMResult(bsm_msg->getSequenceNum(), bsm_msg->getBsmOutcome());
        delete msg;
        return;
    }

    if (strcmp(msg->getName(), SELF_QSDC_PREPARE) == 0) {
        QLOG("[SERVER] Initializing QSDC Protocol. Requesting EndNode Readiness.");
        sendClassicalMessage(0, QSDC_COMM_START, "QSDC_COMM_START"); 
        sendClassicalMessage(4, QSDC_COMM_START, "QSDC_COMM_START");
        delete msg;
        return;
    }

    if (strcmp(msg->getName(), QSDC_COMM_START) == 0) {
        if (is_repeater) {
            send(msg, "toRouter");
            return;
        }
        QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] Received Start. Sending Ready.");
        sendClassicalMessage(2, QSDC_COMM_READY, "QSDC_COMM_READY"); 
        delete msg;
        return;
    }

    if (auto* pkt = dynamic_cast<quisp::messages::QSDCSynAck*>(msg)) {
        if (is_repeater) {
            send(msg, "toRouter");
            return;
        }

        std::string msg_type = pkt->getName();
        std::string from = pkt->getFromNode();

        if (msg_type == QSDC_COMM_SYNC) {
            QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] Session Synced. Buffer cleared. Sending ACK.");
            received_qubits.clear(); 
            ready_qubits.clear();
            bsm_arrival_counts.clear();
            
            // Tell the Server we are safely cleared and ready
            sendClassicalMessage(2, QSDC_COMM_ACK, "QSDC_COMM_ACK"); // 2 is Server
        }
        else if (msg_type == QSDC_COMM_END) {
            QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] PROTOCOL COMPLETE! End Signal Received.");
        }
        else if (is_server && msg_type == QSDC_COMM_READY) {
            if (from == "alice") alice_ready = true;
            if (from == "bob") bob_ready = true;

            if (alice_ready && bob_ready) {
                QLOG("[SERVER] Both nodes ready. Sending Sync to clear buffers.");
                
                // Reset flags for the upcoming ACK phase
                alice_ready = false; 
                bob_ready = false;
                
                sendClassicalMessage(0, QSDC_COMM_SYNC, "QSDC_COMM_SYNC"); 
                sendClassicalMessage(4, QSDC_COMM_SYNC, "QSDC_COMM_SYNC");
            }
        }
        // Initialization Phase 2
        else if (is_server && msg_type == QSDC_COMM_ACK) {
            if (from == "alice") alice_ready = true;
            if (from == "bob") bob_ready = true;

            if (alice_ready && bob_ready) {
                QLOG("[SERVER] EndNodes synced. Starting Qubit Emission FSM.");
                current_qubit_index = 0;
                sendNextQubitPair();
            }
        }
        else if (is_server && msg_type == QSDC_QUBIT_ACK) {
            int rcv_index = pkt->getSequenceNum();
            
            if (rcv_index == current_qubit_index) { // Alice/Bob ACK the Target index
                if (from == "alice") alice_received_current = true;
                if (from == "bob") bob_received_current = true;

                if (alice_received_current && bob_received_current) {
                    QLOG("[SERVER] Target Qubit " << current_qubit_index << " Successfully Purified & Synchronized!");

                    // ADVANCE BY 2 (Since we consumed Target and Source!)
                    current_qubit_index += 2;

                    if (current_qubit_index < total_qubits_to_send) {
                        sendNextQubitPair();
                    } else {
                        QLOG("[SERVER] Transmission Complete. Sending END signal.");
                        sendClassicalMessage(0, QSDC_COMM_END, "Comm_End"); 
                        sendClassicalMessage(4, QSDC_COMM_END, "Comm_End");
                    }
                }
            } 
        }
        else if (msg_type == "QSDC_PURIFY_RESULT") {
            int target_seq = pkt->getSequenceNum();
            int partner_meas = pkt->getMeasResult(); 
            
            // Safe Map Lookup
            int my_meas = my_local_measurements[target_seq]; 
            QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] Comparing Purification Results. Mine: " << my_meas << ", Partner: " << partner_meas);

            if (my_meas == partner_meas) {
                QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] Purification SUCCESS for Qubit " << target_seq);
                
                if (is_alice) {
                    received_qubits[target_seq]->gateZ();
                    received_qubits[target_seq]->gateX();
                }
                
                // CRITICAL FIX 2: Send ACK to Server to progress the FSM
                sendClassicalMessage(2, QSDC_QUBIT_ACK, "QSDC_QUBIT_ACK", target_seq); 
                
            } else {
                QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] Purification FAILED. Requesting Rollback.");
                sendClassicalMessage(2, QSDC_QUBIT_ERROR, "QSDC_QUBIT_ERROR", target_seq); 
                
                // received_qubits[target_seq]->setFree(true);
                received_qubits.erase(target_seq);
            }
        }
        else if (msg_type == QSDC_QUBIT_SYNC) {
            int seq_num = pkt->getSequenceNum();
            QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] Server marked Qubit Index: " << seq_num << " in transit.");
        }
        else if (is_server && msg_type == QSDC_QUBIT_ERROR) {
            int err_index = pkt->getSequenceNum();
            
            if (err_index == current_qubit_index && !server_is_rolling_back) {
                QLOG("[SERVER] ARQ: Error reported on Qubit " << err_index << ". Issuing DISCARD to both nodes.");
                
                server_is_rolling_back = true;
                alice_continue_ready = false;
                bob_continue_ready = false;

                sendClassicalMessage(0, QSDC_QUBIT_DISCARD, "QSDC_QUBIT_DISCARD", current_qubit_index);
                sendClassicalMessage(4, QSDC_QUBIT_DISCARD, "QSDC_QUBIT_DISCARD", current_qubit_index);
            }
        }
        else if (msg_type == QSDC_QUBIT_DISCARD) {
            int discard_index = pkt->getSequenceNum();
            QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] ARQ: Discarding Qubit Index: " << discard_index);
            
            if (received_qubits.find(discard_index) != received_qubits.end()) {
                // received_qubits[discard_index]->setFree(true);
                received_qubits.erase(discard_index);
            }
            
            ready_qubits.erase(std::remove(ready_qubits.begin(), ready_qubits.end(), discard_index), ready_qubits.end());
            
            QLOG("[" << (is_alice ? "ALICE" : "BOB") << "] ARQ: Sending CONTINUE to Server.");
            sendClassicalMessage(2, QSDC_QUBIT_CONTINUE, "QSDC_QUBIT_CONTINUE", discard_index);
        }
        else if (is_server && msg_type == QSDC_QUBIT_CONTINUE) {
            int cont_index = pkt->getSequenceNum();
            
            if (cont_index == current_qubit_index) {
                if (from == "alice") alice_continue_ready = true;
                if (from == "bob") bob_continue_ready = true;

                if (alice_continue_ready && bob_continue_ready) {
                    QLOG("[SERVER] ARQ: Both nodes discarded successfully. Re-emitting Qubit " << current_qubit_index);
                    sendNextQubitPair(); 
                }
            }
        }

        delete msg;
        return;
    }
    
    // (I removed the dead code blocks that were repeating checks at the bottom of the file)
    
    return;
}

}  // namespace quisp::modules

