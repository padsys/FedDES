<p align="center">
  <a><img src="./Logo.png" alt="APPFL logo" style="width: 20%; height: auto;"></a>
</p>
<p align="center" style="font-size: 18px;">
    <b>FedDES: An Open-Source Discrete Event Based Simulator for Federated Learning Systems</b>.
</p>

<p align="center">
| <a href="https://acm-ieee-sec.org/2025/program.php"><b>Paper</b></a> |
</p>

FedDES is a high-fidelity, framework-agnostic discrete-event simulation platform that accurately models the runtime behavior of FL systems, including client training, communication overhead, network dynamics, and aggregation strategies. 

## Table of Contents
- [Table of Contents](#table-of-contents)
- [Project Structure](#project-structure)
- [Requirements](#requirements)
  - [Installing SimGrid](#installing-simgrid)
- [Building](#building)
- [Configuration](#configuration)
  - [Straggler Definition](#straggler-definition)
  - [Platform XML Format](#platform-xml-format)
- [Running Simulations](#running-simulations)
- [Reproducing Results](#reproducing-results)
- [Citation](#citation)
- [Acknowledgements](#acknowledgements)

## Project Structure

```
FedDES/
├── config/                 # JSON configs for each algorithm
├── resources/              # SimGrid platform/network descriptions
├── simulation/
│   ├── algorithm/          # FedAvg/FedAsync/FedCompass sources
│   └── network/            # Platform generators (e.g., ncsa_delta_server_client_generator.py)
└── third_party/            # Vendored single-header deps (nlohmann/json)
```

## Requirements
- C++17 compiler (`c++`, `clang++`, or `g++`)
- SimGrid ≥ 4.0; ensure `simgrid-config` or `pkg-config simgrid` is available in `PATH`
- CMake/make not required; binaries can be built directly via one-line commands

### Installing SimGrid
On macOS (Homebrew):

```sh
brew install simgrid
```

On Ubuntu/Debian, use the packaged version or build from source:

```sh
sudo apt install simgrid-dev        # (if available)
# OR build from source
git clone https://framagit.org/simgrid/simgrid.git
cd simgrid && cmake -DCMAKE_INSTALL_PREFIX=$HOME/.local . && make -j && make install
```

After installation, ensure `$HOME/.local/bin` (or your prefix) is on `PATH` so `simgrid-config`/`pkg-config` can be resolved. Other platforms can follow the official instructions at <https://simgrid.org>.

## Building
Use `simgrid-config` (or `pkg-config`) to obtain the correct include/lib flags, and add `third_party` to the include path for the JSON header. Example builds:

```sh
cd simulation/algorithm
# FedAvg
c++ -std=c++17 FedAvg.cpp \
    -I../../third_party \
    $(simgrid-config --cflags --libs) \
    -o ./bin/des_fedavg

# FedAsync
c++ -std=c++17 FedAsync.cpp \
    -I../../third_party \
    $(simgrid-config --cflags --libs) \
    -o ./bin/des_fedasync

# FedCompass
c++ -std=c++17 FedCompass.cpp \
    -I../../third_party \
    $(simgrid-config --cflags --libs) \
    -o ./bin/des_fedcompass
```

> If `simgrid-config` is not available, replace `$(simgrid-config --cflags --libs)` with `$(pkg-config --cflags --libs simgrid)`.

## Configuration
Each algorithm consumes a JSON file defining topology and costs. All configs share the following core keys:

| Key                | Description                                             |
|--------------------|---------------------------------------------------------|
| `num_nodes`        | # of compute nodes in the platform                      |
| `clients_per_node` | Max clients per node (Node-1 hosts one fewer client)    |
| `epochs`           | Number of global epochs / scheduler iterations          |
| `dataloader_cost`  | Time to simulate data loading (per client/server)       |
| `aggregation_cost` | Time per aggregation step on the server                 |
| `training_cost`    | Time per local client training.                         |
| `comm_cost`        | Bytes for model transfer                                |
| `control`          | Control flag: `0` deterministic, `1` noisy training, `2` also perturbs host speeds |

Algorithm-specific fields:

- **FedCompass**
  - `max_local_steps`: upper bound for local steps
  - `q_ratio`, `lambda`: scheduler hyper-parameters
  - `validation_cost`, `validation_flag`: validation time required

### Straggler Definition
All algorithms accept a `stragglers` array. Each entry must have an `effect` (>0) and one of:
- `client`: integer id
- `clients`: array of ids
- `range`: either `[start, end]` or `{ "start": s, "end": e }`

Effects multiply if multiple rules match a client. Example:

```json
"stragglers": [
  { "client": 0, "effect": 1.5 },
  { "clients": [3, 9, 27], "effect": 2.0 },
  { "range": { "start": 40, "end": 63 }, "effect": 0.7 }
]
```

### Platform XML Format
SimGrid expects a platform description in XML. Each file must define:
- `<platform>` root with `<zone>` elements describing routing domains.
- `<host>` entries with attributes such as `id`, `speed` (FLOPS), and optional `core` count.
- `<link>` entries describing bandwidth/latency (e.g., `bandwidth="200MBps" latency="50us"`).
- `<route>` or `<linkctn>` sections wiring hosts to links.

A minimal excerpt (see `resources/delta_platform.xml` for the full topology):

```xml
<platform version="4">
  <zone id="AS0" routing="Full">
    <host id="Node-1" speed="2.5e9" core="32"/>
    <host id="Node-2" speed="2.5e9" core="32"/>
    <link id="L1" bandwidth="100MBps" latency="20us"/>
    <route src="Node-1" dst="Node-2">
      <link_ctn id="L1"/>
    </route>
  </zone>
</platform>
```

You can script larger networks via the generators in `simulation/network/` (e.g., `ncsa_delta_server_client_generator.py` produces the 16-node Delta layout).

## Running Simulations
All binaries follow the same CLI: `./<binary> <platform.xml> <config.json>`.

```sh
./appfl-fedavg     ../resources/PLATFORM.xml ../config/CONFIG.json
./appfl-fedasync   ../resources/PLATFORM.xml ../config/CONFIG.json
./appfl-fedcompass ../resources/PLATFORM.xml ../config/CONFIG.json
```

Logs are emitted via SimGrid/XBT. Redirect stdout+stderr to capture traces:

```sh
./appfl-fedavg ../resources/PLATFORM.xml ../config/CONFIG.json > run.log 2>&1
```

## Reproducing Results
1. Generate a platform file using scripts in `simulation/network/` (e.g., delta cluster).
2. Build the desired algorithm binary as shown above.
3. Edit the corresponding config JSON with your topology and straggler settings.
4. Run the simulator

## Citation
If you find FedDES useful for your research or development, please consider citing the following paper:
```
@inproceedings{10.1145/3769102.3770613,
author={Chen, Zhonghao and Chen, Weicong and Zhang, Duo and Kim, Kibaek and Li, Guanpeng and Di, Sheng and Lu, Xiaoyi},
title={FedDES: Discrete Event Based Performance Simulation for Federated Learning Systems},
year = {2025},
publisher = {IEEE Press},
url = {https://doi.org/10.1145/3769102.3770613},
doi = {10.1145/3769102.3770613},
booktitle = {Proceedings of the 2025 IEEE/ACM Symposium on Edge Computing},
location = {Arlington, VA, USA},
series = {SEC '25}
}
```

## Acknowledgements

This work was partially supported by NSF research grants #2321123, #2333324, #2340982, and #2505106, and a DOE research grant DE-SC0024207. This research is partially based on research supported by the U.S. DOE Office of Science-Advanced Scientific Computing Research Program, under Contract No. DE-AC02-06CH11357.
