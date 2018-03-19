#include "torch/csrc/autograd/functions/batch_normalization.h"
#include <sstream>

#include "torch/csrc/jit/ir.h"

namespace torch {
namespace autograd {

jit::value_list BatchNormForward::symbolic(
    SymbolicContext* ctx,
    jit::value_list inputs,
    std::shared_ptr<jit::SourceLocation> sl
) {
  auto & g = ctx->graph;
  // X, Scale, Bias
  auto bn_node = g->create(jit::kBatchNormalization,{inputs.at(0),inputs.at(1),inputs.at(2)}, 0);
  bn_node->setSourceLocation(sl);
  auto bn = g->appendNode(bn_node);
  bn->addInput(jit::tracer::getBufferTrace(*ctx->buffer_map, running_mean));
  bn->addInput(jit::tracer::getBufferTrace(*ctx->buffer_map, running_var));
  bn->i_(jit::kis_test, !this->training);
  bn->f_(jit::kepsilon, eps);
  //bn->s_(jit::korder, "NCHW");
  bn->f_(jit::kmomentum, 1 - momentum);

  auto orig_output = bn->addOutput();

  if(this->training) {
    bn->addOutput()->setType(bn->input(3)->type());
    bn->addOutput()->setType(bn->input(4)->type());
    // dummy output
    for(int i = 3; i < 5; i++) {
      auto o = bn->addOutput();
      o->setUniqueName("batch_norm_dead_output-" + o->uniqueName());
    }
  }
  bn->is_(jit::kconsumed_inputs,{0,0,0,1,1});

  ctx->batch_norm_count++;
  return {orig_output};
}


} // torch::autograd
} // torch
