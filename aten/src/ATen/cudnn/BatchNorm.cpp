#include "BatchNorm.h"

#include "Descriptors.h"
#include "Types.h"
#include "Utils.h"

#include <ATen/Check.h>


namespace at { namespace native {

namespace {

Tensor expandScale(const Tensor& t, int64_t dim) {
  std::vector<int64_t> size{ 1, t.numel() };
  while (static_cast<int64_t>(size.size()) < dim) {
    size.emplace_back(1);
  }
  return t.view(size);
}

}  // namespace

Tensor cudnn_batch_norm_forward(
    const Tensor& input_t, const Tensor& weight_t,
    const Tensor& bias_t, const Tensor& running_mean_t, const Tensor& running_var_t,
    const Tensor& save_mean_t, const Tensor& save_var_t, bool training,
    double exponential_average_factor, double epsilon)
{
  TensorArg input{ input_t, "input", 1 },
            weight{ weight_t, "weight", 2 },
            bias{ bias_t, "bias", 3 },
            running_mean{ running_mean_t, "running_mean", 4 },
            running_var{ running_var_t, "running_var", 5 },
            save_mean{ save_mean_t, "save_mean", 6 },
            save_var{ save_var_t, "save_var", 7 };
  CheckedFrom c = "cudnn_batch_norm_forward";
  setCuDNNStreamToCurrent();

  checkAllDefined(c, {input, weight, bias, running_mean, running_var});
  if (training) {
    checkAllDefined(c, {save_mean, save_var});
  }
  checkAllSameGPU(c, {input, weight, bias, running_mean, running_var, save_mean, save_var});
  if (input->type().scalarType() == ScalarType::Half) {
    checkScalarType(c, weight, ScalarType::Float);
  } else {
    checkAllSameType(c, {input, weight});
  }
  checkAllSameType(c, {weight, bias, running_mean, running_var, save_mean, save_var});
  // TODO: is weight required to be contiguous?
  checkAllContiguous(c, {input, weight, bias, running_mean, running_var, save_mean, save_var});
  checkDimRange(c, input, 2, 6 /* exclusive */);
  auto num_features = input->size(1);
  for (auto t : {weight, bias, running_mean, running_var, save_mean, save_var}) {
    checkNumel(c, t, num_features);
  }

  cudnnBatchNormMode_t mode;
  if (input->dim() == 2) {
    mode = CUDNN_BATCHNORM_PER_ACTIVATION;
  } else {
    mode = CUDNN_BATCHNORM_SPATIAL;
#if CUDNN_VERSION >= 7003
    if(training)
      mode = CUDNN_BATCHNORM_SPATIAL_PERSISTENT;
#endif
  }

  auto output_t = input->type().tensor(input->sizes());
  TensorArg output{ output_t, "output", 0 };

  auto handle = getCudnnHandle();
  auto dataType = getCudnnDataType(*input);
  TensorDescriptor idesc{ *input, 4 };  // input descriptor
  TensorDescriptor wdesc{ expandScale(*weight, input->dim()), 4 };  // descriptor for weight, bias, running_mean, etc.

  Constant one(dataType, 1);
  Constant zero(dataType, 0);
  if (training) {
    CUDNN_CHECK(cudnnBatchNormalizationForwardTraining(
      handle, mode, &one, &zero,
      idesc.desc, input->data_ptr(),
      idesc.desc, output->data_ptr(),
      wdesc.desc,
      weight->data_ptr(),
      bias->data_ptr(),
      exponential_average_factor,
      running_mean->data_ptr(),
      running_var->data_ptr(),
      epsilon,
      save_mean->data_ptr(),
      save_var->data_ptr()));
  } else {
    CUDNN_CHECK(cudnnBatchNormalizationForwardInference(
      handle, mode, &one, &zero,
      idesc.desc, input->data_ptr(),
      idesc.desc, output->data_ptr(),
      wdesc.desc,
      weight->data_ptr(),
      bias->data_ptr(),
      running_mean->data_ptr(),
      running_var->data_ptr(),
      epsilon));
  }

  return output_t;
}

std::tuple<Tensor, Tensor, Tensor> cudnn_batch_norm_backward(
    const Tensor& input_t, const Tensor& grad_output_t, const Tensor& weight_t,
    const Tensor& save_mean_t, const Tensor& save_var_t, bool training,
    double epsilon)
{
  TensorArg input{ input_t, "input", 1 },
            grad_output{ grad_output_t, "grad_output", 2 },
            weight{ weight_t, "weight", 3 },
            save_mean{ save_mean_t, "save_mean", 4 },
            save_var{ save_var_t, "save_var", 5 };
  CheckedFrom c = "cudnn_batch_norm_backward";
  setCuDNNStreamToCurrent();

  checkAllDefined(c, {input, grad_output, weight, save_mean, save_var});
  checkAllSameGPU(c, {input, grad_output, weight, save_mean, save_var});
  if (input->type().scalarType() == ScalarType::Half) {
    checkScalarType(c, weight, ScalarType::Float);
  } else {
    checkAllSameType(c, {input, weight});
  }
  checkAllSameType(c, {input, grad_output});
  checkAllSameType(c, {weight, save_mean, save_var});
  // TODO: is weight required to be contiguous?
  checkAllContiguous(c, {input, grad_output, save_mean, save_var});
  checkDimRange(c, input, 2, 6 /* exclusive */);
  checkSameSize(c, input, grad_output);
  auto num_features = input->size(1);
  for (auto t : {weight, save_mean, save_var}) {
    checkNumel(c, t, num_features);
  }

  cudnnBatchNormMode_t mode;
  if (input->dim() == 2) {
    mode = CUDNN_BATCHNORM_PER_ACTIVATION;
  } else {
    mode = CUDNN_BATCHNORM_SPATIAL;
#if CUDNN_VERSION >= 7003
    if(training)
      mode = CUDNN_BATCHNORM_SPATIAL_PERSISTENT;
#endif
  }

  auto grad_input_t  = input->type().tensor(input->sizes());
  auto grad_weight_t = weight->type().tensor(weight->sizes());
  auto grad_bias_t   = weight->type().tensor(weight->sizes());

  auto handle = getCudnnHandle();
  auto dataType = getCudnnDataType(*input);

  TensorDescriptor idesc{ *input, 4 };  // input, output, grad_output descriptor
  TensorDescriptor wdesc{ expandScale(*weight, input->dim()), 4 };  // descriptor for weight, bias, save_mean, etc.

  Constant one(dataType, 1);
  Constant zero(dataType, 0);

  CUDNN_CHECK(cudnnBatchNormalizationBackward(
    handle, mode, &one, &zero, &one, &zero,
    idesc.desc, input->data_ptr(),
    idesc.desc, grad_output->data_ptr(),
    idesc.desc, grad_input_t.data_ptr(),
    wdesc.desc, weight->data_ptr(),
    grad_weight_t.data_ptr(),
    grad_bias_t.data_ptr(),
    epsilon,
    save_mean->data_ptr(),
    save_var->data_ptr()));

  return std::tuple<Tensor,Tensor,Tensor>{grad_input_t, grad_weight_t, grad_bias_t};
}

}}  // namespace native
