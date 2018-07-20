/* Send blaming letters to @yrtimd */
#include "pacmans.hpp"

IPacMan::Task& IPacMan::allocNext() {
  lastElt_ = queue_.lockWrite();
  lastElt_->element.pack.data_ =
    allocator_.allocateNext(Packet::MaxSize);
  return lastElt_->element;
}

void IPacMan::enQueueLast() {
  allocator_.shrinkLast(lastElt_->element.size);
  queue_.unlockWrite(lastElt_);
}

TaskPtr<IPacMan> IPacMan::getNextTask() {
  TaskPtr<IPacMan> result;
  result.owner_ = this;
  result.ptr_ = queue_.lockRead();
  return result;
}

void IPacMan::releaseTask(IPacMan::Queue::Element* elt) {
  queue_.unlockRead(elt);
}

TaskPtr<OPacMan> OPacMan::getNextTask() {
  TaskPtr<OPacMan> result;
  result.owner_ = this;
  result.ptr_ = queue_.lockRead();
  return result;
}

void OPacMan::releaseTask(OPacMan::Queue::Element* elt) {
  queue_.unlockRead(elt);
}

OPacMan::Task& OPacMan::allocNext() {
  lastElt_ = queue_.lockWrite();
  return lastElt_->element;
}

void OPacMan::enQueueLast() {
  queue_.unlockWrite(lastElt_);
}
