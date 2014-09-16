
let execute_command_get_output ?(env=[||]) script args =
  failwith "execute_command_get_output not implemented"

exception Spawn_internal_error of string * string * Unix.process_status
