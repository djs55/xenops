exception Cancelled

module Xenops_task = struct
  type t = unit
  let cancel _ _ = ()
  let with_cancel _task _cancel_cb f = f () (* never cancelled *)
  let raise_cancelled _task = raise Cancelled
end

