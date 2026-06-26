package networks;

import modules.QNode;
import modules.Backend.Backend;
import modules.Logger.Logger;
import modules.SharedResource.SharedResource;
import channels.ClassicalChannel;
import channels.QuantumChannel;

network qsdc_network
{
    parameters:
        @display("bgb=1450,300");

    submodules:
        backend: Backend { @display("p=40,40"); }
        logger: Logger { @display("p=40,90"); }
        sharedResource: SharedResource { @display("p=40,140"); }

        alice: QNode {
            parameters:
                address = 0;
                node_type = "EndNode";
                @display("p=100,150;i=COMP");
        }
        BSArepeater0: QNode {
            parameters:
                address = 10;
                node_type = "QSDCRepeater";
                @display("p=250,150;i=BSA");
        }
        BSArepeater1: QNode {
            parameters:
                address = 20;
                node_type = "QSDCRepeater";
                @display("p=400,150;i=BSA");
        }
        BSArepeater2: QNode {
            parameters:
                address = 30;
                node_type = "QSDCRepeater";
                @display("p=550,150;i=BSA");
        }
        BSArepeater3: QNode {
            parameters:
                address = 40;
                node_type = "QSDCRepeater";
                @display("p=700,150;i=BSA");
        }
        server: QNode {
            parameters:
                address = 2;
                node_type = "QSDCServer";
                @display("p=850,150;i=EPPS");
        }
        BSArepeater4: QNode {
            parameters:
                address = 50;
                node_type = "QSDCRepeater";
                @display("p=1000,150;i=BSA");
        }
        BSArepeater5: QNode {
            parameters:
                address = 60;
                node_type = "QSDCRepeater";
                @display("p=1150,150;i=BSA");
        }
        bob: QNode {
            parameters:
                address = 4;
                node_type = "EndNode";
                @display("p=1300,150;i=COMP");
        }

    connections:
        // --- Standard Channel Connections ---
        alice.port++ <--> ClassicalChannel <--> BSArepeater0.port++;
        alice.quantum_port++ <--> QuantumChannel <--> BSArepeater0.quantum_port++;

        BSArepeater0.port++ <--> ClassicalChannel <--> BSArepeater1.port++;
        BSArepeater0.quantum_port++ <--> QuantumChannel <--> BSArepeater1.quantum_port++;

        BSArepeater1.port++ <--> ClassicalChannel <--> BSArepeater2.port++;
        BSArepeater1.quantum_port++ <--> QuantumChannel <--> BSArepeater2.quantum_port++;

        BSArepeater2.port++ <--> ClassicalChannel <--> BSArepeater3.port++;
        BSArepeater2.quantum_port++ <--> QuantumChannel <--> BSArepeater3.quantum_port++;

        BSArepeater3.port++ <--> ClassicalChannel <--> server.port++;
        BSArepeater3.quantum_port++ <--> QuantumChannel <--> server.quantum_port++;

        server.port++ <--> ClassicalChannel <--> BSArepeater4.port++;
        server.quantum_port++ <--> QuantumChannel <--> BSArepeater4.quantum_port++;

        BSArepeater4.port++ <--> ClassicalChannel <--> BSArepeater5.port++;
        BSArepeater4.quantum_port++ <--> QuantumChannel <--> BSArepeater5.quantum_port++;

        BSArepeater5.port++ <--> ClassicalChannel <--> bob.port++;
        BSArepeater5.quantum_port++ <--> QuantumChannel <--> bob.quantum_port++;

        // ====================================================================
        // Custom Application-Layer Bypass Connections
        // ====================================================================

        // Forward path: left to right (output[1] -> input[0])
        alice.appQuantumOut[1] --> QuantumChannel {
            distance = 2km;
            channel_loss_rate = 0;
            channel_x_error_rate = 0;
            channel_y_error_rate = 0;
            channel_z_error_rate = 0;
        } --> BSArepeater0.appQuantumIn[0];

        BSArepeater0.appQuantumOut[1] --> QuantumChannel {
            distance = 2km;
        } --> BSArepeater1.appQuantumIn[0];

        BSArepeater1.appQuantumOut[1] --> QuantumChannel {
            distance = 2km;
        } --> BSArepeater2.appQuantumIn[0];

        BSArepeater2.appQuantumOut[1] --> QuantumChannel {
            distance = 2km;
        } --> BSArepeater3.appQuantumIn[0];

        BSArepeater3.appQuantumOut[1] --> QuantumChannel {
            distance = 2km;
        } --> server.appQuantumIn[0];

        server.appQuantumOut[1] --> QuantumChannel {
            distance = 2km;
        } --> BSArepeater4.appQuantumIn[0];

        BSArepeater4.appQuantumOut[1] --> QuantumChannel {
            distance = 2km;
        } --> BSArepeater5.appQuantumIn[0];

        BSArepeater5.appQuantumOut[1] --> QuantumChannel {
            distance = 2km;
        } --> bob.appQuantumIn[0];

        // Return path: right to left (output[0] -> input[1])
        bob.appQuantumOut[0] --> QuantumChannel {
            distance = 2km;
        } --> BSArepeater5.appQuantumIn[1];

        BSArepeater5.appQuantumOut[0] --> QuantumChannel {
            distance = 2km;
        } --> BSArepeater4.appQuantumIn[1];

        BSArepeater4.appQuantumOut[0] --> QuantumChannel {
            distance = 2km;
        } --> server.appQuantumIn[1];

        server.appQuantumOut[0] --> QuantumChannel {
            distance = 2km;
        } --> BSArepeater3.appQuantumIn[1];

        BSArepeater3.appQuantumOut[0] --> QuantumChannel {
            distance = 2km;
        } --> BSArepeater2.appQuantumIn[1];

        BSArepeater2.appQuantumOut[0] --> QuantumChannel {
            distance = 2km;
        } --> BSArepeater1.appQuantumIn[1];

        BSArepeater1.appQuantumOut[0] --> QuantumChannel {
            distance = 2km;
        } --> BSArepeater0.appQuantumIn[1];

        BSArepeater0.appQuantumOut[0] --> QuantumChannel {
            distance = 2km;
        } --> alice.appQuantumIn[1];

}
