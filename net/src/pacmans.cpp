/* Send blaming letters to @yrtimd */
#include "pacmans.hpp"

IPacMan::Task& IPacMan::allocNext() {
  if (!lockedLast_) {
    lastElt_ = queue_.lockWrite();
    lockedLast_ = true;

    new (&lastElt_->element) Task();

    lastElt_->element.pack.data_ = allocator_.allocateNext(Packet::MaxSize);
  }

  return lastElt_->element;
}

void IPacMan::enQueueLast() {
  allocator_.shrinkLast(lastElt_->element.size);
  queue_.unlockWrite(lastElt_);
  lockedLast_ = false;
}

TaskPtr<IPacMan> IPacMan::getNextTask() {
  TaskPtr<IPacMan> result;
  result.owner_ = this;
  result.ptr_ = queue_.lockRead();
  return result;
}

void IPacMan::releaseTask(IPacMan::Queue::Element* elt) {
  elt->element.~Task();
  queue_.unlockRead(elt);
}

TaskPtr<OPacMan> OPacMan::getNextTask() {
  TaskPtr<OPacMan> result;
  result.owner_ = this;
  result.ptr_ = queue_.lockRead();
  return result;
}

void OPacMan::releaseTask(OPacMan::Queue::Element* elt) {
  elt->element.~Task();
  queue_.unlockRead(elt);
}

OPacMan::Queue::Element* OPacMan::allocNext() {
  auto elt = queue_.lockWrite();
  new (&elt->element) Task();
  return elt;
}

void OPacMan::enQueueLast(Queue::Element* elt) {
  queue_.unlockWrite(elt);
}
