# SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.

# SPDX-License-Identifier: Apache-2.0
import ttnn
from models.common.lightweightmodule import LightweightModule


class RMSNorm(LightweightModule):
    def __init__(
        self,
        device_mesh,
        state_dict,
        args,
        dtype,
        layer_num,
        weight_key,
        eps: float = 1e-05,
    ):
        super().__init__()
        self.device_mesh = device_mesh
        self.eps = eps
        self.state_dict = state_dict
        self.model_config = args.get_model_config()

        if layer_num is None:
            weight_name = f"{weight_key}.weight"
        else:
            weight_name = f"layers.{layer_num}.{weight_key}.weight"

        torch_weight = self.state_dict[weight_name].unsqueeze(0).expand(32, -1)

        if args.dummy_weights:
            cache_name = None
        else:
            cache_name = args.weight_cache_path(dtype) / (weight_name + "multidevice")

        self.weight = ttnn.as_tensor(
            torch_weight,
            device=self.device_mesh,
            dtype=dtype,
            layout=self.model_config["NORM_W_LAYOUT_TILE"],
            memory_config=self.model_config["NORM_WEIGHTS_MEMCFG"],
            cache_file_name=cache_name,
            mesh_mapper=ttnn.ReplicateTensorToMesh(device_mesh),
        )

    def forward(self, x: ttnn.Tensor) -> ttnn.Tensor:
        x = ttnn.rms_norm(x, weight=self.weight, epsilon=self.eps)
        return x


class RMSNormSharded(LightweightModule):
    def __init__(
        self,
        device_mesh,
        state_dict,
        args,
        dtype,
        layer_num,
        weight_key,
        eps: float = 1e-05,
    ):
        super().__init__()
        self.device_mesh = device_mesh
        self.eps = eps
        self.state_dict = state_dict
        self.model_config = args.get_model_config()

        if layer_num is None:
            weight_name = f"{weight_key}.weight"
        else:
            weight_name = f"layers.{layer_num}.{weight_key}.weight"

        torch_weight = self.state_dict[weight_name].unsqueeze(0).view(1, 1, 4096).expand([1, 32, 4096])
        if args.dummy_weights:
            cache_name = None
        else:
            cache_name = args.weight_cache_path(dtype) / (weight_name + "multidevice")

        self.weight = ttnn.as_tensor(
            torch_weight,
            device=self.device_mesh,
            dtype=dtype,
            layout=ttnn.TILE_LAYOUT,
            memory_config=self.model_config["NORM_WEIGHTS_MEMCFG"],
            cache_file_name=cache_name,
            mesh_mapper=ttnn.ReplicateTensorToMesh(device_mesh),
        )

    def forward(self, x: ttnn.Tensor, out_sharded=False) -> ttnn.Tensor:
        x = ttnn.experimental.tensor.interleaved_to_sharded(
            x, sharded_mem_config=self.model_config["SHARDED_NORM_INPUT_MEMCFG"]
        )
        x = ttnn.rms_norm(
            x,
            epsilon=self.eps,
            weight=self.weight,
            program_config=self.model_config["SHARDED_NORM_PRGM_CFG"],
            memory_config=self.model_config["SHARDED_NORM_OUTPUT_MEMCFG"],
        )
        if out_sharded:
            return x
        x_interleaved = ttnn.experimental.tensor.sharded_to_interleaved(x)
        x.deallocate(True)
        return x_interleaved