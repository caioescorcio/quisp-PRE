
package networks;

import modules.*;
import modules.Backend.Backend;
import modules.Logger.Logger;
import modules.SharedResource.SharedResource;
import channels.ClassicalChannel;
import channels.QuantumChannel;

network ThreeNodeTopology
{
    parameters:
        **.speed_of_light_in_fiber = 208189.206944 km;

    submodules:
        // Global Singletons required by the C++ backend
        backend: Backend;
        logger: Logger;
        sharedResource: SharedResource;
        
        // Node A and Node B (Initiator and Responder)
        EndNode[2]: QNode {
            address = index + 100000;
            node_type = "EndNode";
            @display("i=COMP");
        }
        
        // The intermediate Bell-State Analyzers
        BSA[2]: BSANode {
            address = index + 1000;
            @display("i=BSA");
        }
        
        // The intermediate Swapper (Repeater R)
        Repeater[1]: QNode {
            address = index;
            node_type = "EndNode";
            @display("i=REP2G");
        }
        
    connections:

        EndNode[0].port++ <--> ClassicalChannel { distance = 10km; } <--> BSA[0].port++;
        BSA[0].port++ <--> ClassicalChannel <--> Repeater[0].port++;
        
        EndNode[0].quantum_port++ <--> QuantumChannel { distance = 10km; } <--> BSA[0].quantum_port++;
        BSA[0].quantum_port++ <--> QuantumChannel <--> Repeater[0].quantum_port++;



        EndNode[1].port++ <--> ClassicalChannel { distance = 10km; } <--> BSA[1].port++;
        BSA[1].port++ <--> ClassicalChannel <--> Repeater[0].port++;
        
        EndNode[1].quantum_port++ <--> QuantumChannel { distance = 10km; } <--> BSA[1].quantum_port++;
        BSA[1].quantum_port++ <--> QuantumChannel <--> Repeater[0].quantum_port++;
}