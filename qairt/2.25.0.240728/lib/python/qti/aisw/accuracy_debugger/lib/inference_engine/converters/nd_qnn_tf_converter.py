# =============================================================================
#
#  Copyright (c) 2020 Qualcomm Technologies, Inc.
#  All Rights Reserved.
#  Confidential and Proprietary - Qualcomm Technologies, Inc.
#
# =============================================================================

from qti.aisw.accuracy_debugger.lib.inference_engine import inference_engine_repository
from qti.aisw.accuracy_debugger.lib.utils.nd_constants import ComponentType, Framework, Engine
from qti.aisw.accuracy_debugger.lib.inference_engine.converters.nd_qnn_converter import QNNConverter
from qti.aisw.accuracy_debugger.lib.utils.nd_exceptions import InferenceEngineError
from qti.aisw.accuracy_debugger.lib.utils.nd_errors import get_message


@inference_engine_repository.register(cls_type=ComponentType.converter,
                                      framework=Framework.tensorflow, engine=Engine.QNN,
                                      engine_version="0.5.0.11262")
class QNNTensorflowConverter(QNNConverter):

    def __init__(self, context):
        super(QNNTensorflowConverter, self).__init__(context)
        self.input_tensor_flag = context.arguments["input_tensor_flag"]
        self.output_tensor_flag = context.arguments["output_tensor_flag"]

    def build_convert_command(self, model_path, input_tensors, output_tensors, output_path,
                              input_list_txt, quantization_overrides, param_quantizer,
                              act_quantizer, weight_bw, bias_bw, act_bw, float_bias_bw,
                              restrict_quantization_steps, algorithms, ignore_encodings,
                              per_channel_quantization, act_quantizer_calibration,
                              param_quantizer_calibration, act_quantizer_schema,
                              param_quantizer_schema, percentile_calibration_value,
                              float_fallback, extra_converter_args=None):
        convert_command = [self.executable, self.model_path_flag, model_path]
        for tensor in input_tensors:
            convert_command += [self.input_tensor_flag, "\"" + tensor[0] + "\"", tensor[1]]

        for tensor in output_tensors:
            convert_command += [self.output_tensor_flag, "\"" + tensor + "\""]

        convert_command += [self.output_path_flag, output_path]

        convert_command += self.quantization_command(
            input_list_txt, quantization_overrides, param_quantizer, act_quantizer, weight_bw,
            bias_bw, act_bw, float_bias_bw, restrict_quantization_steps, algorithms,
            ignore_encodings, per_channel_quantization, act_quantizer_calibration,
            param_quantizer_calibration, act_quantizer_schema, param_quantizer_schema,
            percentile_calibration_value, float_fallback)

        convert_command_str = ' '.join(convert_command)
        if extra_converter_args:
            convert_command_str += ' ' + extra_converter_args
        return convert_command_str
