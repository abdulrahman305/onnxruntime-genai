# =============================================================================
#
#  Copyright (c) Qualcomm Technologies, Inc.
#  All Rights Reserved.
#  Confidential and Proprietary - Qualcomm Technologies, Inc.
#
# =============================================================================

from qti.aisw.accuracy_debugger.lib.utils.nd_path_utility import get_absolute_path, format_args
from qti.aisw.accuracy_debugger.lib.options.snooping.qairt_snooping_cmd_options import QAIRTSnoopingCmdOptions
from qti.aisw.accuracy_debugger.lib.utils.nd_exceptions import ParameterError
from qti.aisw.accuracy_debugger.lib.utils.nd_constants import Framework, Engine, Runtime
from qti.aisw.accuracy_debugger.lib.utils.nd_exceptions import InferenceEngineError
from qti.aisw.accuracy_debugger.lib.utils.nd_constants import Architecture_Target_Types, Engine, Runtime, \
    Android_Architectures, X86_Architectures, \
    Device_type, Qnx_Architectures, Windows_Architectures, X86_windows_Architectures, Aarch64_windows_Architectures
from qti.aisw.accuracy_debugger.lib.utils.nd_errors import get_message

import argparse
import os
import numpy as np


class QAIRTCumulativeLayerwiseSnoopingCmdOptions(QAIRTSnoopingCmdOptions):

    def __init__(self, args, validate_args=True):
        super().__init__(args=args, type="cumulative-layerwise", validate_args=validate_args)

    def initialize(self):
        """
        type: (List[str]) -> argparse.Namespace

        :param args: User inputs, fed in as a list of strings
        :return: Namespace object
        """
        self.parser = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter,
                                              description="Script to run snooping.")

        verifier_args = self.parser.add_argument_group('Verifier Arguments')
        optional_args = self.parser.add_argument_group('Optional Arguments')

        verifier_args.add_argument(
            '--default_verifier', type=str.lower, required=True, nargs='+', action="append",
            help='Default verifier used for verification. The options '
            '"RtolAtol", "AdjustedRtolAtol", "TopK", "L1Error", "CosineSimilarity", "MSE", "MAE", "SQNR", "ScaledDiff" are supported. '
            'An optional list of hyperparameters can be appended. For example: --default_verifier rtolatol,rtolmargin,0.01,atolmargin,0.01 '
            'An optional list of placeholders can be appended. For example: --default_verifier CosineSimilarity param1 1 param2 2. '
            'to use multiple verifiers, add additional --default_verifier CosineSimilarity')

        verifier_args.add_argument(
            '--result_csv', type=str, required=False,
            help='Path to the csv summary report comparing the inference vs framework'
            'Paths may be absolute, or relative to the working directory.'
            'if not specified, then a --problem_inference_tensor must be specified')

        verifier_args.add_argument('--verifier_threshold', type=float, default=None,
                                   help='Verifier threshold for problematic tensor to be chosen.')

        verifier_args.add_argument('--verifier_config', type=str, default=None,
                                   help='Path to the verifiers\' config file')
        optional_args.add_argument(
            '--step_size', type=int, required=False, default=1,
            help="number of layers to skip in each iteration of debugging. \
                                     Applicable only for cumulative-layerwise algorithm. \
                                     --step_size (> 1) should not be used along with --add_layer_outputs, \
                                     --add_layer_types, --skip_layer_outputs, skip_layer_types, \
                                     --start_layer, --end_layer")
        self._base_initialize()

        self.initialized = True

    def verify_update_parsed_args(self, parsed_args):
        parsed_args = self._verify_update_base_parsed_args(parsed_args)
        parsed_args.result_csv = get_absolute_path(parsed_args.result_csv)
        supported_verifiers = [
            "rtolatol", "adjustedrtolatol", "topk", "l1error", "cosinesimilarity", "mse", "mae",
            "sqnr", "scaleddiff"
        ]
        for verifier in parsed_args.default_verifier:
            verifier_name = verifier[0].split(',')[0]
            if verifier_name not in supported_verifiers:
                raise ParameterError(
                    f"--default_verifier '{verifier_name}' is not a supported verifier.")

        return parsed_args