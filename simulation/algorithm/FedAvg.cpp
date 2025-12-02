/*
* Copyright (c) 2025, University of California, Merced. All rights reserved.
*
* This file is part of the simulation software package developed by
* the team members of Prof. Xiaoyi Lu's group at University of California, Merced.
*
* For detailed copyright and licensing information, please refer to the license
* file LICENSE in the top level directory.
*
*/

#include <random>
#include <vector>
#include <string>
#include <unordered_map>
#include <fstream>
#include <simgrid/s4u.hpp>
#include "../../third_party/nlohmann/json.hpp"

XBT_LOG_NEW_DEFAULT_CATEGORY(APPFL, "Messages specific for this example");

using json = nlohmann::json;

static void server(std::vector<std::string> args)
{
    xbt_assert(args.size() >= 5, "The server function expects at least 5 arguments");

    int client_count = std::stoi(args[0]);
    long epoch_count = std::stol(args[1]);
    double dataloader_cost = std::stod(args[2]);
    double aggregation_cost = std::stod(args[3]);
    double comm_cost = std::stod(args[4]);

    simgrid::s4u::Host *host = simgrid::s4u::this_actor::get_host();
    double speed = host->get_speed();
    XBT_INFO("Server is running on host: %s", host->get_name().c_str());
    XBT_INFO("Computation speed of the host is: %f FLOPS", speed);

    std::vector<simgrid::s4u::Mailbox *> mailboxes;
    // each client has two mailboxes
    for (unsigned int i = 0; i <= client_count; i++)
        mailboxes.push_back(simgrid::s4u::Mailbox::by_name(std::to_string(i)));

    XBT_INFO("Got %d clients and %ld epochs to process", client_count, epoch_count);

    simgrid::s4u::this_actor::execute(dataloader_cost * speed); // simulate dataload and partitioning

    for (size_t i = 0; i < client_count; i++)
    {
        mailboxes[i]->put(new double(comm_cost), 4); // model size
    }

    for (int round = 0; round < epoch_count; round++)
    {
        XBT_INFO("[Server]: Starting epoch %d of %ld", round + 1, epoch_count);
        for (int i = 0; i < client_count; i++)
        {
            mailboxes[i]->put(new double(1), comm_cost * 8);
            simgrid::s4u::this_actor::execute(0.05 * speed);
            XBT_INFO("Step 1.%04d: Server sent global model size and model to client %d", i, i);
        }
        int arrival_client_count = 0;
        while (arrival_client_count < client_count)
        {
            int *client_id = mailboxes[client_count]->get<int>();
            simgrid::s4u::this_actor::execute(0.17 * speed);
            XBT_INFO("Step 4.%04d: received local model from client %d", *client_id, *client_id);
            arrival_client_count++;
        }
    }
}

static void client(std::vector<std::string> args)
{
    // Create a random device to seed the generator
    std::random_device rd;

    // Create a Mersenne Twister pseudo-random number generator
    std::mt19937 gen(rd());

    // Create a uniform_real_distribution to generate floating-point numbers between 1 and 2
    std::normal_distribution<double> dist(0, 0.12);

    xbt_assert(args.size() >= 5, "The client expects at least 5 argument");

    simgrid::s4u::Host *my_host = simgrid::s4u::this_actor::get_host();

    int client_id = std::stoi(args[0]);
    int client_count = std::stoi(args[1]);
    int num_epochs = std::stoi(args[2]);
    double dataloader_cost = std::stod(args[3]);
    double training_cost = std::stod(args[4]);
    int control = std::stoi(args[5]);

    simgrid::s4u::Host *host = simgrid::s4u::this_actor::get_host();
    double speed = host->get_speed();
    if (control == 2)
        speed *= dist(gen);

    simgrid::s4u::this_actor::execute(dataloader_cost * speed); // simulate dataload and partitioning

    simgrid::s4u::Mailbox *my_mailbox = simgrid::s4u::Mailbox::by_name(std::to_string(client_id));        // wait for data
    simgrid::s4u::Mailbox *server_mailbox = simgrid::s4u::Mailbox::by_name(std::to_string(client_count)); // wait for data

    double *comm_cost = nullptr;
    comm_cost = my_mailbox->get<double>();
    for (int i = 0; i < num_epochs + 1; i++)
    {
        my_mailbox->get<double>();
        XBT_INFO("Step 2.%04d: Client %04d Received global model from server (%f bytes)", client_id, client_id, *comm_cost);
        if (control == 0)
            simgrid::s4u::this_actor::execute(training_cost * speed);
        else
            simgrid::s4u::this_actor::execute(training_cost * speed * dist(gen));
        server_mailbox->put(&client_id, *comm_cost * 32); // send local model to server
        XBT_INFO("Step 3.%04d: Client %04d sent updated model to server (%f bytes)", client_id, client_id, *comm_cost);
    }
}

json load_config(const char *config_arg)
{
    xbt_assert(config_arg != nullptr, "Missing JSON configuration argument");
    std::ifstream file(config_arg);
    if (file.good())
    {
        try
        {
            return json::parse(file);
        }
        catch (const json::parse_error &e)
        {
            xbt_die("Failed to parse configuration file %s: %s", config_arg, e.what());
        }
    }

    try
    {
        return json::parse(config_arg);
    }
    catch (const json::parse_error &e)
    {
        xbt_die("Failed to parse configuration JSON string: %s", e.what());
    }
}

std::unordered_map<int, double> parse_client_effects(const json &rules, int total_clients)
{
    std::unordered_map<int, double> effects;
    if (rules.is_null())
        return effects;

    xbt_assert(rules.is_array(), "Straggler configuration must be a JSON array");

    for (const auto &rule : rules)
    {
        xbt_assert(rule.contains("effect"), "Each straggler rule must define an \"effect\"");
        double effect = rule["effect"].get<double>();
        xbt_assert(effect > 0.0, "Straggler effect must be positive (got %f)", effect);

        bool applied = false;
        auto apply_to_client = [&](int client_id) {
            xbt_assert(client_id >= 0 && client_id < total_clients, "Invalid straggler client %d (valid range: 0-%d)", client_id, total_clients - 1);
            applied = true;
            auto it = effects.find(client_id);
            if (it == effects.end())
            {
                effects[client_id] = effect;
            }
            else
            {
                it->second *= effect;
            }
        };

        if (rule.contains("client"))
        {
            xbt_assert(rule["client"].is_number_integer(), "\"client\" must be an integer");
            apply_to_client(rule["client"].get<int>());
        }

        if (rule.contains("clients"))
        {
            xbt_assert(rule["clients"].is_array(), "\"clients\" must be an array of integers");
            for (const auto &client_val : rule["clients"])
            {
                xbt_assert(client_val.is_number_integer(), "Each entry in \"clients\" must be an integer");
                apply_to_client(client_val.get<int>());
            }
        }

        if (rule.contains("range"))
        {
            const auto &range_val = rule["range"];
            int start_client = 0;
            int end_client = 0;
            if (range_val.is_array())
            {
                xbt_assert(range_val.size() == 2, "\"range\" array must contain exactly two integers");
                xbt_assert(range_val[0].is_number_integer() && range_val[1].is_number_integer(), "\"range\" entries must be integers");
                start_client = range_val[0].get<int>();
                end_client = range_val[1].get<int>();
            }
            else
            {
                xbt_assert(range_val.is_object(), "\"range\" must be an object or a two-value array");
                xbt_assert(range_val.contains("start") && range_val.contains("end"), "\"range\" object must contain \"start\" and \"end\"");
                xbt_assert(range_val["start"].is_number_integer() && range_val["end"].is_number_integer(), "\"start\" and \"end\" must be integers");
                start_client = range_val["start"].get<int>();
                end_client = range_val["end"].get<int>();
            }
            xbt_assert(start_client <= end_client, "\"range\" start must be <= end");
            for (int client = start_client; client <= end_client; ++client)
            {
                apply_to_client(client);
            }
        }

        xbt_assert(applied, "Straggler rule must target at least one client");
    }

    return effects;
}

int main(int argc, char *argv[])
{
    xbt_assert(argc >= 3, "Usage: %s <platform_file> <config_json_or_path>", argv[0]);

    simgrid::s4u::Engine e(&argc, argv);

    e.load_platform(argv[1]);

    json config = load_config(argv[2]);

    // Register server and client functions
    e.register_function("server", &server);
    e.register_function("client", &client);

    int num_nodes = config.at("num_nodes").get<int>();
    int nclients_pernode = config.at("clients_per_node").get<int>(); // Node-1 hosts one fewer client
    int control = config.value("control", 0);

    // Define parameters for the simulation
    int nclients = num_nodes * nclients_pernode - 1;
    int nepochs = config.at("epochs").get<int>();
    double dataloader_cost = config.at("dataloader_cost").get<double>();
    double aggregation_cost = config.at("aggregation_cost").get<double>();
    double training_cost = config.at("training_cost").get<double>();
    double comm_cost = config.at("comm_cost").get<double>();

    json straggler_rules = config.contains("stragglers") ? config["stragglers"] : json::array();
    std::unordered_map<int, double> client_effects = parse_client_effects(straggler_rules, nclients);

    auto client_multiplier = [&](int client_id) -> double {
        auto it = client_effects.find(client_id);
        if (it == client_effects.end())
            return 1.0;
        return it->second;
    };

    // Create the server actor on host "Node-1"
    std::vector<std::string> server_args = {std::to_string(nclients), std::to_string(nepochs),
                                            std::to_string(dataloader_cost), std::to_string(aggregation_cost),
                                            std::to_string(comm_cost)};
    simgrid::s4u::Actor::create("server", simgrid::s4u::Host::by_name("Node-1"), server, server_args);

    // Distribute clients across multiple nodes
    int client_id = 0;

    for (int i = 0; i < nclients_pernode - 1 && client_id < nclients; ++i, ++client_id)
    {
        double multiplier = client_multiplier(client_id);
        double node_dataloader_cost = dataloader_cost * multiplier;
        double node_training_cost = training_cost * 0.8 * multiplier;
        std::vector<std::string> client_args = {std::to_string(client_id), std::to_string(nclients), std::to_string(nepochs), std::to_string(node_dataloader_cost), std::to_string(node_training_cost), std::to_string(control)};
        simgrid::s4u::Actor::create("client", simgrid::s4u::Host::by_name("Node-1"), client, client_args);
    }

    int node_index = 2;
    while (client_id < nclients)
    {
        std::string node_name = "Node-" + std::to_string(node_index);
        for (int i = 0; i < nclients_pernode && client_id < nclients; ++i, ++client_id)
        {
            double multiplier = client_multiplier(client_id);
            double node_dataloader_cost = dataloader_cost * multiplier;
            double node_training_cost = training_cost * multiplier;
            std::vector<std::string> client_args = {std::to_string(client_id), std::to_string(nclients), std::to_string(nepochs), std::to_string(node_dataloader_cost), std::to_string(node_training_cost), std::to_string(control)};
            simgrid::s4u::Actor::create("client", simgrid::s4u::Host::by_name(node_name), client, client_args);
        }
        ++node_index;
    }

    // Run the simulation
    e.run();

    XBT_INFO("Simulation is over");

    return 0;
}
