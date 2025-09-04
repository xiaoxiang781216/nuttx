#!/usr/bin/env python3
# tools/cpufreq/cql.py
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to you under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import json
import os
import random
import struct

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
import yaml
from tqdm import tqdm

CONFIG_PATH = "./cql_config.yaml"


def load_config(config_path):
    with open(config_path, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)
    return cfg


config = load_config(CONFIG_PATH)

DEVICE = torch.device(
    config["device"]
    if torch.cuda.is_available() and config["device"] == "cuda"
    else "cpu"
)

if config["device"] == "cuda" and not torch.cuda.is_available():
    print("[WARN] CUDA is configured but not available. Switched to CPU.")

RANDOM_SEED = config["random_seed"]
np.random.seed(RANDOM_SEED)
random.seed(RANDOM_SEED)
torch.manual_seed(RANDOM_SEED)
if torch.cuda.is_available():
    torch.cuda.manual_seed_all(RANDOM_SEED)

STATE_DIM = config["state_dim"]
HIDDEN_DIM = config["hidden_dim"]
ACTION_DIM = config["action_dim"]

LEARNING_RATE = config["learning_rate"]
GAMMA = config["gamma"]
TARGET_UPDATE_FREQ = config["target_update_freq"]
ALPHA = config["alpha"]
BATCH_SIZE = config["batch_size"]
NUM_EPOCHS = config["num_epochs"]
EVAL_INTERVAL = config["eval_interval"]

DATA_PATH = config["data_path"]
MODEL_SAVE_DIR = config["model_save_dir"]
FINAL_MODEL_NAME = config["final_model_name"]
CHECKPOINT_MODEL_PATTERN = config["checkpoint_model_pattern"]

FINAL_MODEL_PATH = os.path.join(MODEL_SAVE_DIR, FINAL_MODEL_NAME)
CHECKPOINT_MODEL_PATH_TEMPLATE = os.path.join(MODEL_SAVE_DIR, CHECKPOINT_MODEL_PATTERN)

REWARD_POWER_WEIGHT = config["reward"]["power_weight"]
REWARD_FPS_WEIGHT = config["reward"]["fps_weight"]
REWARD_FPS_TARGET = config["reward"]["fps_target"]
FREQ_POWER_MAPPING = {
    int(k): float(v) for k, v in config.get("freq_power_mapping", {}).items()
}

NUM_LAYERS = config["num_layers"]
ARRAY_SIZE = config["params_array_size"]

ACTION_FREQ_MAPPING = config["action_freq_mapping"]
ACTION_NAMES = config["action_names"]


def load_and_preprocess_data(file_path):
    with open(file_path, "r") as f:
        data_dict = json.load(f)

    length = len(data_dict)
    inst_list = [float(data_dict[i]["inst"]) for i in range(length)]
    cache_list = [float(data_dict[i]["cache"]) for i in range(length)]
    freq_list = [int(data_dict[i]["freq"]) for i in range(length)]
    cycle_list = [float(data_dict[i]["cycle"]) for i in range(length)]
    fps_list = [int(data_dict[i]["fps"]) for i in range(length)]
    action_list = [int(data_dict[i]["action"]) for i in range(length)]

    for i in range(length):
        inst_list[i] = inst_list[i] / cycle_list[i]
        cache_list[i] = cache_list[i] / cycle_list[i]

    inst_mean, inst_std = np.mean(inst_list), np.std(inst_list)
    cache_mean, cache_std = np.mean(cache_list), np.std(cache_list)
    mean = [inst_mean, cache_mean]
    std = [inst_std, cache_std]

    inst_list = [(x - inst_mean) / inst_std for x in inst_list]
    cache_list = [(x - cache_mean) / cache_std for x in cache_list]

    dataset = []
    for i in range(length - 1):
        state = [inst_list[i], cache_list[i]]
        action = action_list[i] - 2
        r_fps = float(fps_list[i + 1]) / REWARD_FPS_TARGET
        freq = freq_list[i + 1]
        r_power = FREQ_POWER_MAPPING.get(freq, 1)
        reward = REWARD_POWER_WEIGHT * r_power + REWARD_FPS_WEIGHT * r_fps

        next_state = [inst_list[i + 1], cache_list[i + 1]]
        done = False

        dataset.append((state, action, reward, next_state, done))

    return dataset, mean, std


class QNetwork(nn.Module):
    def __init__(self, state_dim, hidden_dim, action_dim):
        super(QNetwork, self).__init__()
        self.fc1 = nn.Linear(state_dim, hidden_dim)
        self.fc2 = nn.Linear(hidden_dim, hidden_dim)
        self.fc3 = nn.Linear(hidden_dim, action_dim)
        nn.init.xavier_normal_(self.fc1.weight)
        nn.init.xavier_normal_(self.fc2.weight)
        nn.init.xavier_normal_(self.fc3.weight)

    def forward(self, x):
        x = F.relu(self.fc1(x))
        x = F.relu(self.fc2(x))
        return self.fc3(x)


class OfflineReplayBuffer:
    def __init__(self, dataset):
        self.states = np.array([x[0] for x in dataset], dtype=np.float32)
        self.actions = np.array([x[1] for x in dataset], dtype=np.int64)
        self.rewards = np.array([x[2] for x in dataset], dtype=np.float32)
        self.next_states = np.array([x[3] for x in dataset], dtype=np.float32)
        self.dones = np.array([x[4] for x in dataset], dtype=np.float32)
        self.size = len(dataset)

    def sample(self, batch_size):
        indices = np.random.randint(0, self.size, size=batch_size)
        return (
            self.states[indices],
            self.actions[indices],
            self.rewards[indices],
            self.next_states[indices],
            self.dones[indices],
        )

    def __len__(self):
        return self.size


class CQL:
    def __init__(
        self, state_dim, hidden_dim, action_dim, lr, gamma, target_update, alpha, device
    ):
        self.action_dim = action_dim
        self.q_net = QNetwork(state_dim, hidden_dim, action_dim).to(device)
        self.target_q_net = QNetwork(state_dim, hidden_dim, action_dim).to(device)
        self.target_q_net.load_state_dict(self.q_net.state_dict())

        self.optimizer = torch.optim.Adam(self.q_net.parameters(), lr=lr)
        self.gamma = gamma
        self.target_update = target_update
        self.alpha = alpha
        self.count = 0
        self.device = device

    def take_action(self, state, epsilon=0.01):
        if np.random.random() < epsilon:
            return np.random.randint(self.action_dim)
        state = torch.FloatTensor(state).unsqueeze(0).to(self.device)
        return self.q_net(state).argmax().item()

    def update(self, samples):
        states, actions, rewards, next_states, dones = samples
        states = torch.FloatTensor(states).to(self.device)
        actions = torch.LongTensor(actions).to(self.device)
        rewards = torch.FloatTensor(rewards).unsqueeze(1).to(self.device)
        next_states = torch.FloatTensor(next_states).to(self.device)
        dones = torch.FloatTensor(dones).unsqueeze(1).to(self.device)

        curr_q = self.q_net(states).gather(1, actions.unsqueeze(1))
        with torch.no_grad():
            max_next_q = self.target_q_net(next_states).max(1)[0].unsqueeze(1)
            target_q = rewards + self.gamma * max_next_q * (1 - dones)

        td_loss = F.mse_loss(curr_q, target_q)

        batch_q = self.q_net(states)
        logsumexp_q = torch.logsumexp(batch_q, dim=1, keepdim=True)
        cql_loss = (logsumexp_q - curr_q).mean()

        total_loss = td_loss + self.alpha * cql_loss

        self.optimizer.zero_grad()
        total_loss.backward()
        self.optimizer.step()

        if self.count % self.target_update == 0:
            self.target_q_net.load_state_dict(self.q_net.state_dict())
        self.count += 1

        return total_loss.item()

    def save(self, path):
        torch.save(self.q_net.state_dict(), path)

    def load(self, path):
        self.q_net.load_state_dict(torch.load(path))
        self.target_q_net.load_state_dict(self.q_net.state_dict())


def eval_policy(agent, dataset, n_eval=100):
    states = torch.FloatTensor(np.array([x[0] for x in dataset[:n_eval]])).to(DEVICE)
    actions = torch.LongTensor(np.array([x[1] for x in dataset[:n_eval]])).to(DEVICE)
    rewards = np.array([x[2] for x in dataset[:n_eval]])

    with torch.no_grad():
        policy_actions = agent.q_net(states).argmax(dim=1)
        match_rate = (policy_actions == actions).float().mean().item()
        q_values = agent.q_net(states).gather(1, actions.unsqueeze(1))
        avg_q = q_values.mean().item()

    print("\nEvaluation Results:")
    print(f"Matching original actions: {match_rate * 100:.1f}%")
    print(f"Average Q value: {avg_q:.3f}")
    print(f"Average reward: {np.mean(rewards):.3f}\n")
    return match_rate, avg_q, np.mean(rewards)


def print_network_weights(net, network_name="target_net"):
    print(f"// {network_name} params")
    print(f"const float {network_name}_weights[] = {{")

    for name, param in net.named_parameters():
        if "weight" in name:
            layer_type = "weight"
        elif "bias" in name:
            layer_type = "bias"
        else:
            layer_type = "unknown"

        print(f"    // {name} ({layer_type})")

        weights = param.data.cpu().numpy().flatten().tolist()
        print("    ", end="")
        for i, w in enumerate(weights):
            print(f"{repr(w)}, ", end="")
        print("\n")

    print("};")


def train_offline_rl():
    lr = LEARNING_RATE
    gamma = GAMMA
    target_update = TARGET_UPDATE_FREQ
    alpha = ALPHA
    batch_size = BATCH_SIZE
    num_epochs = NUM_EPOCHS
    eval_interval = EVAL_INTERVAL

    dataset, _, _ = load_and_preprocess_data(DATA_PATH)
    replay_buffer = OfflineReplayBuffer(dataset)

    agent = CQL(
        STATE_DIM, HIDDEN_DIM, ACTION_DIM, lr, gamma, target_update, alpha, DEVICE
    )

    losses = []
    for epoch in range(1, num_epochs + 1):
        num_batches = len(replay_buffer) // batch_size
        epoch_loss = 0.0

        with tqdm(total=num_batches, desc=f"Epoch {epoch}/{num_epochs}") as pbar:
            for _ in range(num_batches):
                samples = replay_buffer.sample(batch_size)
                loss = agent.update(samples)
                epoch_loss += loss
                pbar.set_postfix(
                    {"loss": f"{loss:.4f}", "avg": f"{epoch_loss/(pbar.n+1):.4f}"}
                )
                pbar.update(1)

        losses.append(epoch_loss / num_batches)

        if epoch % eval_interval == 0:
            print(f"\n[Epoch {epoch}] Evaluating...")
            eval_policy(agent, dataset)
            checkpoint_path = CHECKPOINT_MODEL_PATH_TEMPLATE.format(epoch)
            agent.save(checkpoint_path)
            print(f"Model saved to: {checkpoint_path}")

    agent.save(FINAL_MODEL_PATH)
    print(f"Training complete. Final model saved to: {FINAL_MODEL_PATH}")


def export_params(model, mean, std, filename):
    relu = 0
    layers_config = [
        {"input_size": STATE_DIM, "output_size": HIDDEN_DIM, "activation": relu},
        {"input_size": HIDDEN_DIM, "output_size": HIDDEN_DIM, "activation": relu},
        {"input_size": HIDDEN_DIM, "output_size": ACTION_DIM, "activation": relu},
    ]
    with open(filename, "wb") as f:
        f.write(struct.pack("i", NUM_LAYERS))
        for cfg in layers_config:
            input_size = cfg["input_size"]
            output_size = cfg["output_size"]
            activation = cfg["activation"]
            f.write(struct.pack("iii", input_size, output_size, activation))

            if input_size == STATE_DIM and output_size == HIDDEN_DIM:
                weights = model.q_net.fc1.weight.data.numpy().astype(np.float32)
                biases = model.q_net.fc1.bias.data.numpy().astype(np.float32)
            elif input_size == HIDDEN_DIM and output_size == HIDDEN_DIM:
                weights = model.q_net.fc2.weight.data.numpy().astype(np.float32)
                biases = model.q_net.fc2.bias.data.numpy().astype(np.float32)
            elif input_size == HIDDEN_DIM and output_size == ACTION_DIM:
                weights = model.q_net.fc3.weight.data.numpy().astype(np.float32)
                biases = model.q_net.fc3.bias.data.numpy().astype(np.float32)
            else:
                raise ValueError("Invalid layer configuration")

            weight_flat = weights.flatten()
            bias_flat = biases.flatten()

            weights_padded = list(weight_flat) + [0.0] * (
                ARRAY_SIZE * ARRAY_SIZE - len(weight_flat)
            )
            f.write(struct.pack(f"{ARRAY_SIZE * ARRAY_SIZE}f", *weights_padded))

            biases_padded = list(bias_flat) + [0.0] * (ARRAY_SIZE - len(bias_flat))
            f.write(struct.pack(f"{ARRAY_SIZE}f", *biases_padded))

            output_padded = [0.0] * ARRAY_SIZE
            f.write(struct.pack(f"{ARRAY_SIZE}f", *output_padded))

        mean_padded = list(mean) + [0.0] * (ARRAY_SIZE - len(mean))
        f.write(struct.pack(f"{ARRAY_SIZE}f", *mean_padded))

        std_padded = list(std) + [0.0] * (ARRAY_SIZE - len(std))
        f.write(struct.pack(f"{ARRAY_SIZE}f", *std_padded))


if __name__ == "__main__":
    if not os.path.exists(FINAL_MODEL_PATH):
        train_offline_rl()

    print("[INFO] Final model detected. Loading and evaluating...")
    agent = CQL(
        STATE_DIM,
        HIDDEN_DIM,
        ACTION_DIM,
        LEARNING_RATE,
        GAMMA,
        TARGET_UPDATE_FREQ,
        ALPHA,
        DEVICE,
    )
    agent.load(FINAL_MODEL_PATH)
    _, mean, std = load_and_preprocess_data(DATA_PATH)
    eval_policy(agent, _, n_eval=100)
    print_network_weights(agent.q_net, "Online Network (Q)")
    print_network_weights(agent.target_q_net, "Target Network")

    export_params(agent, mean, std, "config_qlearning_params.bin")
