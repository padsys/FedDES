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

#include <algorithm> // For std::sort
#include <fstream>
#include <map>
#include <simgrid/s4u.hpp>
#include <string>
#include <unordered_map>
#include <utility> // For std::pair
#include <vector>
#include <unordered_set>
#include <random>
#include "../../third_party/nlohmann/json.hpp"

XBT_LOG_NEW_DEFAULT_CATEGORY(APPFL_PDES, "Messages specific for this example");

using json = nlohmann::json;

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

template <typename T, typename... Args>
void delayed_action(double delay_in_seconds, void (T::*member_func)(Args...), T *object, Args... args)
{
    XBT_INFO("Delayed action starts timing for %f seconds", delay_in_seconds);
    // Sleep for the specified delay
    simgrid::s4u::this_actor::sleep_for(delay_in_seconds);

    XBT_INFO("Delayed action finishes timing");

    // Call the member function with the provided arguments
    (object->*member_func)(args...);
}

class ServerFedCompass
{
public:
    int counter, global_step, general_buffer_size;
    std::map<int, int> step;
    std::map<int, int> group_pseudo_grad; // key: group_idx, value: _counter

    // simgrid s4u properties
    simgrid::s4u::Host *host;
    double host_speed;

    ServerFedCompass(int num_clients)
    {
        counter = 0;
        global_step = 0;
        general_buffer_size = 0;

        // initilization of s4u properties:
        this->host = simgrid::s4u::this_actor::get_host();
        this->host_speed = host->get_speed();
    }

    void update()
    {
        simgrid::s4u::this_actor::execute(0.03 * host_speed); // aggregation cost per client
        global_step += 1;
    }

    /**
     * @brief Buffer the local gradient from the client of a certain group.
     *
     * @param init_step
     * @param client_idx
     * @param group_idx
     */
    void buffer(int init_step, int client_idx, int group_idx)
    {
        if (group_pseudo_grad.find(group_idx) != group_pseudo_grad.end())
        {
            group_pseudo_grad[group_idx] = 0;
        }
        simgrid::s4u::this_actor::execute(0.01 * host_speed); // TODO
        group_pseudo_grad[group_idx]++;
    }

    void single_buffer(int client_idx)
    {
        simgrid::s4u::this_actor::execute(0.01 * host_speed); // TODO
        general_buffer_size++;
    }

    /**
     * @brief Update the model using all the buffered gradients for a certain group.
     *
     * @param group_idx
     */
    void update_group(int group_idx)
    {
        if (group_pseudo_grad.find(group_idx) == group_pseudo_grad.end())
        {
            simgrid::s4u::this_actor::execute(0.01 * host_speed); // TODO
            global_step++;
            general_buffer_size = 0;
            group_pseudo_grad.erase(group_idx);
        }
    }

    void update_all()
    {
        simgrid::s4u::this_actor::execute(0.0 * host_speed); // TODO
        global_step++;
    }
};

class ClientInfo
{
public:
    int step, local_steps, goa, total_steps;
    double speed, start_time;

    ClientInfo()
    {
        step = -1;
        total_steps = -1;
        local_steps = -1;
        goa = -1;
        speed = -1.0;
        start_time = 0.0;
    }
};

class GOA
{
public:
    std::vector<int> clients, arrived_clients;
    double expected_arrival_time, latest_arrival_time;

    GOA()
    {
        expected_arrival_time = 0.0;
        latest_arrival_time = 0.0;
    }
};

class SchedulerCompass
{
public:
    int iter, num_clients, num_global_epochs, group_counter, max_local_steps, min_local_steps, max_local_steps_bound;
    double SPEED_MOMENTUM, LATEST_TIME_FACTOR, start_time;
    ServerFedCompass *server;
    std::vector<ClientInfo *> client_info;
    std::map<int, GOA *> group_of_arrival;
    std::unordered_set<int> &pending_clients;

    // simgrid s4u properties
    simgrid::s4u::Host *host;
    double host_speed;
    int model_size;
    std::vector<simgrid::s4u::Mailbox *> mailboxes;

    SchedulerCompass(int max_local_steps, int num_clients, int num_global_epochs, int model_size, const std::vector<simgrid::s4u::Mailbox *> &mailboxes, std::unordered_set<int> &pending_clients, double q_ratio = 0.2, double lambda_val = 1.5) : pending_clients(pending_clients)
    {
        this->iter = 0;
        this->num_clients = num_clients;
        this->num_global_epochs = num_global_epochs;
        this->group_counter = 0;
        this->max_local_steps = max_local_steps;
        this->min_local_steps = std::max(static_cast<int>(q_ratio * this->max_local_steps), 1);
        this->max_local_steps_bound = static_cast<int>(1.2 * this->max_local_steps);
        this->SPEED_MOMENTUM = 0.9;
        this->LATEST_TIME_FACTOR = lambda_val;
        this->start_time = simgrid::s4u::Engine::get_clock();
        this->server = new ServerFedCompass(num_clients);
        for (int i = 0; i < num_clients; i++)
        {
            this->client_info.push_back(nullptr);
        }

        // initilization of s4u properties:
        this->host = simgrid::s4u::this_actor::get_host();
        this->host_speed = host->get_speed();
        this->model_size = model_size;
        this->mailboxes = mailboxes;
    }

    void _record_info(int client_idx)
    {
        double curr_time = simgrid::s4u::Engine::get_clock() - start_time;
        double local_start_time = client_info[client_idx] == nullptr ? 0.0 : client_info[client_idx]->start_time;
        double local_update_time = curr_time - local_start_time;
        int local_steps = client_info[client_idx] == nullptr ? max_local_steps : client_info[client_idx]->local_steps;
        double local_speed = local_update_time / local_steps;
        if (!client_info[client_idx])
        {
            client_info[client_idx] = new ClientInfo();
            client_info[client_idx]->speed = local_speed;
            client_info[client_idx]->step = 0;
            client_info[client_idx]->total_steps = min_local_steps;
        }
        else
        {
            client_info[client_idx]->speed = (1.0 - SPEED_MOMENTUM) * client_info[client_idx]->speed + SPEED_MOMENTUM * local_speed;
        }
    }

    int _recv_local_model_from_client()
    {
        int* client_idx = mailboxes[num_clients]->get<int>(); // the value got will be discarded since it will be just a signal
        simgrid::s4u::this_actor::execute(0.15 * host_speed);
        pending_clients.erase(*client_idx);
        XBT_INFO("Step 4.%04d: Received local model from Client %d. Current pending clients: %ld", *client_idx, *client_idx, pending_clients.size());
        return *client_idx;
    }

    bool _join_group(int client_idx)
    {
        double curr_time = simgrid::s4u::Engine::get_clock() - start_time;
        int assigned_group = -1; // assigned group for the client
        int assigned_steps = -1; // assigned local training steps for the client
        for (auto &group : group_of_arrival)
        {
            double remaining_time = group.second->expected_arrival_time - curr_time;
            int local_steps = static_cast<int>(remaining_time / client_info[client_idx]->speed);
            if (local_steps < min_local_steps || local_steps < assigned_steps || local_steps > max_local_steps_bound)
            {
                continue;
            }
            else
            {
                assigned_steps = local_steps;
                assigned_group = group.first;
            }
        }
        if (assigned_group != -1)
        {
            client_info[client_idx]->goa = assigned_group;
            client_info[client_idx]->local_steps = assigned_steps;
            client_info[client_idx]->start_time = curr_time;
            group_of_arrival[assigned_group]->clients.push_back(client_idx);
            XBT_INFO("Client %d - Join GOA %d - Local step %d, At time %f", client_idx, assigned_group, assigned_steps, curr_time);
            return true;
        }
        else
        {
            return false;
        }
    }

    void _create_group(int client_idx)
    {
        double curr_time = simgrid::s4u::Engine::get_clock() - start_time;
        int assigned_steps = -1;
        for (auto &group : group_of_arrival)
        {
            if (curr_time < group.second->latest_arrival_time)
            {
                double fastest_speed = std::numeric_limits<double>::infinity();
                std::vector<int> group_clients;
                group_clients.insert(group_clients.end(), group.second->clients.begin(), group.second->clients.end());
                group_clients.insert(group_clients.end(), group.second->arrived_clients.begin(), group.second->arrived_clients.end());
                for (int client : group_clients)
                {
                    fastest_speed = std::min(fastest_speed, client_info[client]->speed);
                }
                double est_arrival_time = group.second->latest_arrival_time + fastest_speed * max_local_steps;
                int local_steps = static_cast<int>(est_arrival_time - curr_time) / client_info[client_idx]->speed;
                if (local_steps <= max_local_steps)
                {
                    assigned_steps = std::max(assigned_steps, local_steps);
                }
            }
        }
        assigned_steps = (assigned_steps >= 0 && assigned_steps < min_local_steps) ? min_local_steps : assigned_steps;
        assigned_steps = (assigned_steps < 0) ? max_local_steps : assigned_steps;

        // Create a group for the client
        group_of_arrival[group_counter] = new GOA();
        group_of_arrival[group_counter]->clients.push_back(client_idx);
        group_of_arrival[group_counter]->expected_arrival_time = curr_time + assigned_steps * client_info[client_idx]->speed;
        group_of_arrival[group_counter]->latest_arrival_time = curr_time + assigned_steps * client_info[client_idx]->speed * LATEST_TIME_FACTOR;

        XBT_INFO("Group %d created at %f with expected arrival time: %f", group_counter, curr_time, group_of_arrival[group_counter]->expected_arrival_time);
        XBT_INFO("Client %d joined group %d at time %f", client_idx, group_counter, curr_time);

        auto group_aggregation_lambda = [this, curr_time]()
        {
            XBT_INFO("Delayed action in create_group");
            delayed_action(group_of_arrival[group_counter]->latest_arrival_time - curr_time, &SchedulerCompass::_group_aggregation, this, group_counter);
        };
        simgrid::s4u::Actor::create("group_aggregation_actor_" + std::to_string(group_counter), simgrid::s4u::this_actor::get_host(), group_aggregation_lambda);
        client_info[client_idx]->goa = group_counter;
        client_info[client_idx]->local_steps = assigned_steps;
        client_info[client_idx]->start_time = curr_time;
        XBT_INFO("Client %d - Create GOA %d - Local steps %d - At time %f", client_idx, group_counter, assigned_steps, curr_time);
        group_counter++;
    }

    void _send_global_model_to_client(int client_idx, int client_steps)
    {
        XBT_INFO("New global model generated, now sending the new model to Client %d with %d step size", client_idx, client_steps);
        mailboxes[client_idx]->put(new int(client_steps), model_size);
        simgrid::s4u::this_actor::execute(0.047 * host_speed);
        pending_clients.insert(client_idx);
        XBT_INFO("Step 1.%04d: New global model sent, starting next epoch. Current pending clients: %ld", client_idx, pending_clients.size());
    }

    void _send_model(int client_idx)
    {
        client_info[client_idx]->total_steps += client_info[client_idx]->local_steps;
        XBT_INFO("Total number of steps for client %d is %d", client_idx, client_info[client_idx]->total_steps);
        int client_steps = client_info[client_idx]->local_steps;
        _send_global_model_to_client(client_idx, client_steps);
    }

    void _group_aggregation(int group_idx)
    {
        if (group_of_arrival.find(group_idx) != group_of_arrival.end())
        {
            server->update_group(group_idx);
            std::vector<std::pair<int, double>> client_speed;
            for (auto &client : group_of_arrival[group_idx]->arrived_clients)
            {
                client_info[client]->step = server->global_step;
                client_speed.push_back(std::make_pair(client, client_info[client]->speed));
            }
            std::sort(client_speed.begin(), client_speed.end(),
                      [](const std::pair<int, double> &a, const std::pair<int, double> &b)
                      {
                          return a.second < b.second; // Reverse sorting
                      });
            group_of_arrival[group_idx]->expected_arrival_time = 0.0;
            group_of_arrival[group_idx]->latest_arrival_time = 0.0;
            for (auto &client : client_speed)
            {
                _assign_group(client.first);
            }
            // delete the group is not waiting any client
            if (group_of_arrival[group_idx]->clients.size() == 0)
            {
                group_of_arrival.erase(group_idx);
                double curr_time = simgrid::s4u::Engine::get_clock() - start_time;
                XBT_INFO("Group %d is deleted at time %f", group_idx, curr_time);
            }
            if (iter < num_global_epochs)
            {
                for (auto &client : client_speed)
                {
                    _send_model(client.first);
                }
            }
            else
            {
                server->update_all();
            }
        }
    }

    /**
     * @brief Assign a group to the client or create a new group for it when no suitable one exists.
     *
     * @param client_idx
     */
    void _assign_group(int client_idx)
    {
        double curr_time = simgrid::s4u::Engine::get_clock() - start_time;
        if (!group_of_arrival.size())
        {
            group_of_arrival[group_counter] = new GOA();
            group_of_arrival[group_counter]->clients.push_back(client_idx);
            group_of_arrival[group_counter]->expected_arrival_time = curr_time + max_local_steps * client_info[client_idx]->speed;
            group_of_arrival[group_counter]->latest_arrival_time = curr_time + client_info[client_idx]->speed * LATEST_TIME_FACTOR;
            XBT_INFO("Group %d created at %f with expected arrival time %f", group_counter, curr_time, group_of_arrival[group_counter]->expected_arrival_time);
            XBT_INFO("Client %d joined group %d at time %f", client_idx, group_counter, curr_time);
            auto group_aggregation_lambda = [this, curr_time]()
            {
                XBT_INFO("Delayed action in assign_group");
                delayed_action(group_of_arrival[group_counter]->latest_arrival_time - curr_time, &SchedulerCompass::_group_aggregation, this, group_counter);
            };
            simgrid::s4u::Actor::create("group_aggregation_actor_" + std::to_string(group_counter), simgrid::s4u::this_actor::get_host(), group_aggregation_lambda);
            client_info[client_idx]->goa = group_counter;
            client_info[client_idx]->local_steps = max_local_steps;
            client_info[client_idx]->start_time = curr_time;
            XBT_INFO("Client %d - Create GOA %d - Local steps %d - At time %f", client_idx, group_counter, max_local_steps, curr_time);
            group_counter++;
        }
        else
        {
            if (!_join_group(client_idx))
            {
                _create_group(client_idx);
            }
        }
    }

    /**
     * @brief Update the global model using the local model itself
     *
     * @param client_idx
     * @param buffer
     */
    void _single_update(int client_idx, bool buffer)
    {
        if (buffer)
        {
            server->single_buffer(client_idx);
        }
        else
        {
            server->update();
        }
        client_info[client_idx]->step = server->global_step;
        _assign_group(client_idx);
        if (iter < num_global_epochs)
        {
            _send_model(client_idx);
        }
        else
        {
            server->update_all();
        }
    }

    void _group_update(int client_idx, int group_idx)
    {
        double curr_time = simgrid::s4u::Engine::get_clock() - start_time;
        // Update directly if the client arrives late
        if (curr_time >= group_of_arrival[group_idx]->latest_arrival_time)
        {
            group_of_arrival[group_idx]->clients.erase(std::remove(group_of_arrival[group_idx]->clients.begin(), group_of_arrival[group_idx]->clients.end(), client_idx));
            if (group_of_arrival[group_idx]->clients.size() == 0)
            {
                group_of_arrival.erase(group_idx);
                XBT_INFO("Client %d arrived (late) at group %d at time %f", client_idx, group_idx, curr_time);
            }
            _single_update(client_idx, true);
        }
        else
        {
            group_of_arrival[group_idx]->clients.erase(std::remove(group_of_arrival[group_idx]->clients.begin(), group_of_arrival[group_idx]->clients.end(), client_idx));
            group_of_arrival[group_idx]->arrived_clients.push_back(client_idx);
            XBT_INFO("Client %d arrived at group %d at time %f", client_idx, group_idx, curr_time);
            server->buffer(client_info[client_idx]->step, client_idx, group_idx);
            if (group_of_arrival[group_idx]->clients.size() == 0)
            {
                _group_aggregation(group_idx);
            }
        }
    }

    void _update(int client_idx)
    {
        this->iter++;
        int group_idx = client_info[client_idx]->goa;
        if (group_idx == -1)
        {
            _single_update(client_idx, false);
        }
        else
        {
            _group_update(client_idx, group_idx);
        }
    }

    void update()
    {
        int client_idx = _recv_local_model_from_client();
        _record_info(client_idx);
        _update(client_idx);
    }
};

static void server(std::vector<std::string> args)
{
    xbt_assert(args.size() >= 10, "The server function expects at least 10 arguments");

    int num_clients = std::stoi(args[0]);
    long num_epochs = std::stol(args[1]);
    int max_local_steps = std::stoi(args[2]);
    double q_ratio = std::stod(args[3]);
    double lambda_val = std::stod(args[4]);
    double dataloader_cost = std::stod(args[5]);
    double aggregation_cost = std::stod(args[6]);
    double validation_cost = std::stod(args[7]);
    double model_size = std::stod(args[8]);
    bool validation_flag = std::stoi(args[9]);
    std::unordered_set<int> pending_clients;

    simgrid::s4u::Host *host = simgrid::s4u::this_actor::get_host();
    double host_speed = host->get_speed();
    XBT_INFO("Server is running on host: %s", host->get_name().c_str());
    XBT_INFO("Computation speed of the host is: %f FLOPS", host_speed);

    std::vector<simgrid::s4u::Mailbox *> mailboxes;

    // initialize mailboxes. Each client has two, first is server -> client, second is client -> server
    for (unsigned int i = 0; i <= num_clients; i++)
        mailboxes.push_back(simgrid::s4u::Mailbox::by_name(std::to_string(i)));

    XBT_INFO("Got %d clients and %ld epochs to process", num_clients, num_epochs);

    simgrid::s4u::this_actor::execute(dataloader_cost * host_speed); // simulate dataload and partitioning

    // broadcast global model to client
    for (size_t i = 0; i < num_clients; i++)
    {
        XBT_INFO("Broadcasting global model size and model to client %zu", i);
        mailboxes[i]->put(new int(model_size), 4); // model size
        mailboxes[i]->put(new int(max_local_steps), model_size);
        simgrid::s4u::this_actor::execute(0.047 * host_speed);
        XBT_INFO("Step 1.%04ld: Broadcast global model to client %ld", i, i);
        pending_clients.insert(i);
    }

    // Obtain the scheduler
    SchedulerCompass *scheduler = new SchedulerCompass(max_local_steps, num_clients, num_epochs, model_size, mailboxes, pending_clients, q_ratio, lambda_val);

    int global_step = 0;
    while (true)
    {
        XBT_INFO("Starting epoch %d of %ld", global_step + 1, num_epochs);
        scheduler->update();
        global_step++;
        if (validation_flag || global_step == num_epochs)
        {
            simgrid::s4u::this_actor::execute(0.1 * host_speed); // TODO: Measure validation workloads
            if (global_step == num_epochs)
            {
                break;
            }
        }
    }
    XBT_INFO("All rounds have been completed. Requesting all clients to stop. Current pending clients at server is %ld", pending_clients.size());
    while(!pending_clients.empty())
    {
        int *client_id = mailboxes[num_clients]->get<int>();
        simgrid::s4u::this_actor::execute(0.15 * host_speed);
        int temp = *client_id;
        XBT_INFO("Step 5.%04d: Received client %d in cleanup", temp, temp);
        pending_clients.erase(*client_id);
    }
    for(int i = 0; i < num_clients; i++){
        mailboxes[i]->put(new int(-1), 0);
        simgrid::s4u::this_actor::execute(0.03 * host_speed);
        // XBT_INFO("Step 5.%04d: Sent termination signal to client %d", i, i);
    }
    XBT_INFO("Exiting.");
}

static void client(std::vector<std::string> args)
{
    // Create a random device to seed the generator
    std::random_device rd;

    // Create a Mersenne Twister pseudo-random number generator
    std::mt19937 gen(rd());

    // Create a uniform_real_distribution to generate floating-point numbers between 1 and 2
    std::normal_distribution<double> dist(0, 0.12);

    xbt_assert(args.size() >= 6, "The client expects at least 6 arguments");

    simgrid::s4u::Host *my_host = simgrid::s4u::this_actor::get_host();

    int client_id = std::stoi(args[0]);
    int num_clients = std::stoi(args[1]);
    int max_local_steps = std::stoi(args[2]);
    double dataloader_cost = std::stod(args[3]);
    double per_step_training_cost = std::stod(args[4]);
    int control = std::stoi(args[5]);

    simgrid::s4u::Host *host = simgrid::s4u::this_actor::get_host();
    double speed = host->get_speed();
    if (control == 2)
        speed *= dist(gen);
    XBT_INFO("Running on host: %s. Host speed is %f FLOPS", host->get_name().c_str(), speed);

    simgrid::s4u::this_actor::execute(dataloader_cost * speed); // simulate dataload and partitioning

    simgrid::s4u::Mailbox *my_mailbox = simgrid::s4u::Mailbox::by_name(std::to_string(client_id));       // server -> client
    simgrid::s4u::Mailbox *server_mailbox = simgrid::s4u::Mailbox::by_name(std::to_string(num_clients)); // client -> server

    int *model_size = nullptr;
    model_size = my_mailbox->get<int>();
    int *num_local_steps = nullptr;
    while (true)
    {
        // XBT_INFO("Waiting for global model from server");
        num_local_steps = my_mailbox->get<int>();
        if (*num_local_steps < 0)
        {
            XBT_INFO("Client has finished all epochs. Now terminating.");
            break;
        }
        else{
            XBT_INFO("Step 2.%04d: Received new global model from server (%d bytes) with %d step size", client_id, *model_size, *num_local_steps);
        }
        double local_training = per_step_training_cost * (*num_local_steps) * speed;
        if (control != 0)
            local_training *= dist(gen);
        simgrid::s4u::this_actor::execute(local_training);
        XBT_INFO("Finished local training with %d step size, sending local model to the server", *num_local_steps);
        server_mailbox->put(&client_id, *model_size); // send local model to server
        XBT_INFO("Step 3.%04d: Client %d sent local model to the server", client_id, client_id);
    }
}

int main(int argc, char *argv[])
{
    xbt_assert(argc >= 3, "Usage: %s <platform_file> <config_json_or_path>", argv[0]);

    simgrid::s4u::Engine e(&argc, argv);

    e.load_platform(argv[1]);

    json config = load_config(argv[2]);

    // Register server and client functions (for xml-based deployment)
    // e.register_function("server", &server);
    // e.register_function("client", &client);

    int num_nodes = config.at("num_nodes").get<int>();
    int num_clients_per_node = config.at("clients_per_node").get<int>();

    int num_clients = num_nodes * num_clients_per_node - 1;
    int max_local_steps = config.at("max_local_steps").get<int>();
    long num_epochs = config.at("epochs").get<long>();
    double q_ratio = config.value("q_ratio", 0.2);
    double lambda_val = config.value("lambda", 1.5);
    double dataloader_cost = config.at("dataloader_cost").get<double>();
    double aggregation_cost = config.at("aggregation_cost").get<double>();
    double validation_cost = config.value("validation_cost", 0.0);
    double per_step_training_cost = config.at("training_cost").get<double>();
    double model_size = config.at("model_size").get<double>();
    int validation_flag = config.value("validation_flag", 0);
    int control = config.value("control", 0);

    json straggler_rules = config.contains("stragglers") ? config["stragglers"] : json::array();
    std::unordered_map<int, double> client_effects = parse_client_effects(straggler_rules, num_clients);

    auto client_multiplier = [&](int client_id) -> double {
        auto it = client_effects.find(client_id);
        if (it == client_effects.end())
            return 1.0;
        return it->second;
    };

    // Create the server actor on host "Node-1"
    std::vector<std::string> server_args = {std::to_string(num_clients), std::to_string(num_epochs), std::to_string(max_local_steps),
                                            std::to_string(q_ratio), std::to_string(lambda_val), std::to_string(dataloader_cost), std::to_string(aggregation_cost),
                                            std::to_string(validation_cost), std::to_string(model_size), std::to_string(validation_flag)};
    simgrid::s4u::Actor::create("server", simgrid::s4u::Host::by_name("Node-1"), server, server_args);

    // Distribute clients across multiple nodes
    int client_id = 0;

    for (int i = 0; i < num_clients_per_node - 1 && client_id < num_clients; ++i, ++client_id)
    {
        double multiplier = client_multiplier(client_id);
        std::vector<std::string> client_args = {std::to_string(client_id), std::to_string(num_clients), std::to_string(max_local_steps),
                                                std::to_string(dataloader_cost * multiplier), std::to_string(per_step_training_cost * multiplier),
                                                std::to_string(control)};
        simgrid::s4u::Actor::create("Client " + std::to_string(client_id), simgrid::s4u::Host::by_name("Node-1"), client, client_args);
    }

    int node_index = 2;
    while (client_id < num_clients)
    {
        std::string node_name = "Node-" + std::to_string(node_index);
        for (int i = 0; i < num_clients_per_node && client_id < num_clients; ++i, ++client_id)
        {
            double multiplier = client_multiplier(client_id);
            std::vector<std::string> client_args = {std::to_string(client_id), std::to_string(num_clients), std::to_string(max_local_steps),
                                                    std::to_string(dataloader_cost * multiplier), std::to_string(per_step_training_cost * multiplier),
                                                    std::to_string(control)};
            simgrid::s4u::Actor::create("Client " + std::to_string(client_id), simgrid::s4u::Host::by_name(node_name), client, client_args);
        }
        ++node_index;
    }

    // Run the simulation
    e.run();

    XBT_INFO("Simulation is over");

    return 0;
}
