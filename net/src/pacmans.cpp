/* Send blaming letters to @yrtimd */
#include "pacmans.hpp"

IPacMan::Task& IPacMan::allocNext() {
  new (&lastElt_) Task();
  lastElt_.pack.data_ = allocator_.allocateNext(Packet::MaxSize);

  return lastElt_;
}

void IPacMan::enQueueLast() {
  allocator_.shrinkLast(static_cast<uint32_t>(lastElt_.size));
  while(!queue_.push(lastElt_));
  lastElt_.~Task();
}

TaskPtr<IPacMan> IPacMan::getNextTask() {
  TaskPtr<IPacMan> result;
  result.owner_ = this;
  Task *elt = new Task;
  while (!queue_.pop(*elt));
  result.ptr_ = elt;
  return result;
}

void IPacMan::releaseTask(Task* elt) {
  delete elt;
}

OPacMan::Task* OPacMan::allocNext() {
  new (&lastElt_) Task();
  return &lastElt_;
}

void OPacMan::enQueueLast() {
  while (!queue_.push(lastElt_));
  lastElt_.~Task();
}

TaskPtr<OPacMan> OPacMan::getNextTask() {
  TaskPtr<OPacMan> result;
  result.owner_ = this;
  Task *elt = new Task;
  while (!queue_.pop(*elt));
  result.ptr_ = elt;
  return result;
}

void OPacMan::releaseTask(Task* elt) {
  delete elt;
}
