#ifndef ROWNUM_CONTROLLER_H
#define ROWNUM_CONTROLLER_H
class Rownum_controller {
  longlong counter;
  bool has_rownum_increased;

 public:
  Rownum_controller():counter(1), has_rownum_increased(false) {}

  void set_increased(bool flag) {
    has_rownum_increased = flag;
  }

  longlong get_counter() {
    return has_rownum_increased ? (counter - 1) : counter;
  }

  void increase_rownum() {
    ++counter;
    has_rownum_increased = true;
  }

  void init() {
    counter = 1;
    has_rownum_increased = false;
  }
};
#endif /* ROWNUM_CONTROLLER_H */

