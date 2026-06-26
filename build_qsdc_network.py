import os

def generate_qsdc_files(message, num_left, num_right, ned_filename="./quisp/networks/qsdc_network.ned", ini_filename="./quisp/simulations/qsdc_network.ini"):
    """
    Generates QuISP NED and INI files for a QSDC network with a variable 
    number of repeaters between Alice-Server and Server-Bob.
    """


    # 1. Construct the Ordered Node Chain
    nodes = []
    nodes.append({"name": "alice", "type": "EndNode", "addr": 0, "icon": "COMP"})
    
    repeater_index = 0
    
    # Left Repeaters (Alice -> Server)
    for _ in range(num_left):
        nodes.append({
            "name": f"BSArepeater{repeater_index}", 
            "type": "QSDCRepeater", 
            "addr": 10 * (repeater_index + 1), 
            "icon": "BSA"
        })
        repeater_index += 1
        
    # Server
    nodes.append({"name": "server", "type": "QSDCServer", "addr": 2, "icon": "EPPS"})
    
    # Right Repeaters (Server -> Bob)
    for _ in range(num_right):
        nodes.append({
            "name": f"BSArepeater{repeater_index}", 
            "type": "QSDCRepeater", 
            "addr": 10 * (repeater_index + 1), 
            "icon": "BSA"
        })
        repeater_index += 1
        
    # Bob
    nodes.append({"name": "bob", "type": "EndNode", "addr": 4, "icon": "COMP"})

    # 2. Build the NED File Content
    total_nodes = len(nodes)
    bgb_width = 100 + (total_nodes * 150)
    
    ned_str = "package networks;\n\n"
    ned_str += "import modules.QNode;\n"
    ned_str += "import modules.Backend.Backend;\n"
    ned_str += "import modules.Logger.Logger;\n"
    ned_str += "import modules.SharedResource.SharedResource;\n"
    ned_str += "import channels.ClassicalChannel;\n"
    ned_str += "import channels.QuantumChannel;\n\n"
    
    ned_str += "network qsdc_network\n{\n"
    ned_str += "    parameters:\n"
    ned_str += f"        @display(\"bgb={bgb_width},300\");\n\n"
    ned_str += "    submodules:\n"
    ned_str += "        backend: Backend { @display(\"p=40,40\"); }\n"
    ned_str += "        logger: Logger { @display(\"p=40,90\"); }\n"
    ned_str += "        sharedResource: SharedResource { @display(\"p=40,140\"); }\n\n"
    
    # Generate node submodules
    for i, node in enumerate(nodes):
        x_pos = 100 + (i * 150)
        ned_str += f"        {node['name']}: QNode {{\n"
        ned_str += "            parameters:\n"
        ned_str += f"                address = {node['addr']};\n"
        ned_str += f"                node_type = \"{node['type']}\";\n"
        ned_str += f"                @display(\"p={x_pos},150;i={node['icon']}\");\n"
        ned_str += "        }\n"
        
    ned_str += "\n    connections:\n"
    
    # Standard Classical & Quantum Connections
    ned_str += "        // --- Standard Channel Connections ---\n"
    for i in range(total_nodes - 1):
        n1 = nodes[i]["name"]
        n2 = nodes[i+1]["name"]
        ned_str += f"        {n1}.port++ <--> ClassicalChannel <--> {n2}.port++;\n"
        ned_str += f"        {n1}.quantum_port++ <--> QuantumChannel <--> {n2}.quantum_port++;\n\n"
        
    # Application Layer Connections (Forward Path)
    ned_str += "        // ====================================================================\n"
    ned_str += "        // Custom Application-Layer Bypass Connections\n"
    ned_str += "        // ====================================================================\n\n"
    ned_str += "        // Forward path: left to right (output[1] -> input[0])\n"
    
    for i in range(total_nodes - 1):
        n1 = nodes[i]["name"]
        n2 = nodes[i+1]["name"]
        
        ned_str += f"        {n1}.appQuantumOut[1] --> QuantumChannel {{\n"
        ned_str += "            distance = 2km;\n"
        if i == 0:
            ned_str += "            channel_loss_rate = 0;\n"
            ned_str += "            channel_x_error_rate = 0;\n"
            ned_str += "            channel_y_error_rate = 0;\n"
            ned_str += "            channel_z_error_rate = 0;\n"
        ned_str += f"        }} --> {n2}.appQuantumIn[0];\n\n"
        
    # Application Layer Connections (Return Path)
    ned_str += "        // Return path: right to left (output[0] -> input[1])\n"
    for i in range(total_nodes - 1, 0, -1):
        n1 = nodes[i]["name"]
        n2 = nodes[i-1]["name"]
        
        ned_str += f"        {n1}.appQuantumOut[0] --> QuantumChannel {{\n"
        ned_str += "            distance = 2km;\n"
        ned_str += f"        }} --> {n2}.appQuantumIn[1];\n\n"
        
    ned_str += "}\n"

    # 3. Build the INI File Content
    # The wildcard configurations natively support N repeaters.
    ini_str = '''[General]
network = networks.qsdc_network
sim-time-limit = 10s

*.logger.log_filename = "qsdc_starter.log"

# === Payload and Phase Error Rates ===
*.alice.app.payload = "hello world"
*.alice.app.phase1_max_error_rate = 0.1
*.alice.app.phase2_max_error_rate = 0.1

**.buffers = 256 

**.qnic*.num_buffer = 256
**.qnic_r*.num_buffer = 256

*.alice.app.expected_bsms = 1
*.bob.app.expected_bsms = 1

**.alice.app.secret_message = "''' + message + '''"

*.requestedPairs = 256
*.alice.app.number_of_bellpair = 256
*.server.app.number_of_bellpair = 5
*.BSArepeater*.app.number_of_bellpair = 5
*.bob.app.number_of_bellpair = 5
*.alice.app.min_pairs_to_start = 256

# === Phase 1 Configuration ===
*.alice.app.sample_target = 64
*.alice.app.sample_block_size = 8

# === Phase 2 Configuration ===
*.alice.app.bell_sample_target = 64
*.alice.app.bell_block_size = 8

# === Eve Configuration ===
*.alice.app.eve_enabled = true
*.alice.app.eve_intercept_probability = 0.25

# === Timings and Protocol State ===
**.EndToEndConnection = true
**.initial_notification_timing_buffer = 0s

# INI topological mapping
*.alice.app.is_alice = true
*.bob.app.is_bob = true
*.BSArepeater*.app.is_repeater = true
*.BSArepeater*.app.burn_count = 0
*.bob.app.burn_count = 0
*.server.app.burn_count = 0
*.server.app.is_server = true
*.server.app.is_test = true

*.alice.app.start_delay = 50us
*.alice.app.poll_interval = 10us
*.alice.app.sample_interval = 10us
*.alice.app.expect_anti = false
*.alice.app.burn_count = 0

# === Hardware / Error Modeling ===
**.qrsa.hm.link_tomography = false
**.qrsa.hm.initial_purification = 0
**.qrsa.hm.purification_type = "none"

**.memory_x_error_rate = 0
**.memory_y_error_rate = 0
**.memory_z_error_rate = 0
**.memory_energy_excitation_rate = 0
**.memory_energy_relaxation_rate = 0
**.memory_completely_mixed_rate = 0

**.measurement_x_err_rate = 0
**.measurement_y_err_rate = 0
**.measurement_z_err_rate = 0

**.h_gate_err_rate = 0
**.h_gate_x_err_ratio = 0
**.h_gate_y_err_ratio = 0
**.h_gate_z_err_ratio = 0

**.x_gate_err_rate = 0
**.x_gate_x_err_ratio = 0
**.x_gate_y_err_ratio = 0
**.x_gate_z_err_ratio = 0

**.z_gate_err_rate = 0
**.z_gate_x_err_ratio = 0
**.z_gate_y_err_ratio = 0
**.z_gate_z_err_ratio = 0

**.cnot_gate_err_rate = 0

seed-set = ${repetition}
'''

    # 4. Write out the files
    with open(ned_filename, "w") as f_ned:
        f_ned.write(ned_str)
    
    with open(ini_filename, "w") as f_ini:
        f_ini.write(ini_str)
        
    print(f"[+] Successfully generated topology:")
    print(f"    - {num_left} repeaters (Alice <-> Server)")
    print(f"    - {num_right} repeaters (Server <-> Bob)")
    print(f"    - Output NED: {ned_filename}")
    print(f"    - Output INI: {ini_filename}")


if __name__ == "__main__":
    NUM_REPEATERS_ALICE_SERVER = 4
    NUM_REPEATERS_SERVER_BOB = 2
    MESSAGE = "python"
    generate_qsdc_files(MESSAGE, NUM_REPEATERS_ALICE_SERVER, NUM_REPEATERS_SERVER_BOB)