(*
 * Copyright (C) 2006-2009 Citrix Systems Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; version 2.1 only. with the special
 * exception on linking described in file LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *)

open Xenops_utils

(** Associate a task with each active thread *)
let thread_tasks : (int, string) Hashtbl.t = Hashtbl.create 256 
let thread_tasks_m = Mutex.create ()

let get_thread_id () =
  try Thread.id (Thread.self ()) with _ -> -1 

let associate_thread_with_task task = 
  let id = get_thread_id () in
  if id <> -1
  then begin
    Mutex.execute thread_tasks_m (fun () -> Hashtbl.add thread_tasks id task); 
  end

let get_task_from_thread () = 
  let id = get_thread_id () in
  Mutex.execute thread_tasks_m 
    (fun () -> if Hashtbl.mem thread_tasks id then Some(Hashtbl.find thread_tasks id) else None)

let dissociate_thread_from_task () =
  let id = get_thread_id () in
  if id <> -1
  then match get_task_from_thread () with
  | Some _ ->
      Mutex.execute thread_tasks_m (fun () -> Hashtbl.remove thread_tasks id)
  | None ->
	  ()

let with_thread_associated task f x = 
  associate_thread_with_task task;
  try
    let result = f x in
    dissociate_thread_from_task ();
    result
  with e ->
    dissociate_thread_from_task ();
    raise e
    
let threadnames = Hashtbl.create 256
let tnmutex = Mutex.create () 
module StringSet = Set.Make(struct type t=string let compare=Pervasives.compare end)
let debug_keys = ref StringSet.empty 
let get_all_debug_keys () =
	StringSet.fold (fun key keys -> key::keys) !debug_keys []

let dkmutex = Mutex.create ()

let _ = Hashtbl.add threadnames (-1) "no thread"

let get_thread_id () =
    try Thread.id (Thread.self ()) with _ -> -1 

let name_thread name =
    let id = get_thread_id () in
    Mutex.execute tnmutex (fun () -> Hashtbl.add threadnames id name)

let remove_thread_name () =
    let id = get_thread_id () in
    Mutex.execute tnmutex (fun () -> Hashtbl.remove threadnames id)

module type BRAND = sig
  val name: string
end

let hostname_cache = ref None
let hostname_m = Mutex.create ()
let get_hostname () =
  match Mutex.execute hostname_m (fun () -> !hostname_cache) with
  | Some h -> h
  | None ->
		let h = Unix.gethostname () in
		Mutex.execute hostname_m (fun () -> hostname_cache := Some h);
		h
let invalidate_hostname_cache () = Mutex.execute hostname_m (fun () -> hostname_cache := None)

type level = [
  | `Debug
  | `Info
  | `Warning
  | `Err
]

let string_of_level = function
| `Debug   -> "DEBUG"
| `Info    -> "INFO"
| `Warning -> "WARNING"
| `Err     -> "ERROR"

let all_levels = [ `Debug; `Info; `Warning; `Err]
let logging_disabled_for : (string * level) list ref = ref []
let logging_disabled_for_m = Mutex.create ()

let disable ?level brand =
	let levels = match level with
		| None -> all_levels
		| Some l -> [l]
	in
	Mutex.execute logging_disabled_for_m (fun () ->
		let disable' brand level = logging_disabled_for := (brand, level) :: !logging_disabled_for in
		List.iter (disable' brand) levels
	)

let enable ?level brand =
	let levels = match level with
		| None -> all_levels
		| Some l -> [l]
	in
	Mutex.execute logging_disabled_for_m (fun () ->
		logging_disabled_for := List.filter (fun (x, y) -> not (x = brand && List.mem y levels)) !logging_disabled_for
	)

let is_disabled brand level =
	Mutex.execute logging_disabled_for_m (fun () ->
		List.mem (brand, level) !logging_disabled_for
	)

let gettimestring () =
	let time = Unix.gettimeofday () in
	let tm = Unix.gmtime time in
	let msec = time -. (floor time) in
	Printf.sprintf "%d%.2d%.2dT%.2d:%.2d:%.2d.%.3dZ|" (1900 + tm.Unix.tm_year)
		(tm.Unix.tm_mon + 1) tm.Unix.tm_mday
		tm.Unix.tm_hour tm.Unix.tm_min tm.Unix.tm_sec
		(int_of_float (1000.0 *. msec))

let log = ref (fun level message ->
  Printf.fprintf stderr "%s: %s\n%!" (string_of_level level) message
)

module type DEBUG = sig
	val debug : ('a, unit, string, unit) format4 -> 'a

	val warn : ('a, unit, string, unit) format4 -> 'a

	val info : ('a, unit, string, unit) format4 -> 'a

	val error : ('a, unit, string, unit) format4 -> 'a

	val audit : ?raw:bool -> ('a, unit, string, string) format4 -> 'a

	val log_backtrace : unit -> unit

	val log_and_ignore_exn : (unit -> unit) -> unit
end

module Make = functor(Brand: BRAND) -> struct
  let _ =
    Mutex.execute dkmutex (fun () -> 
      debug_keys := StringSet.add Brand.name !debug_keys)

  let get_thread_name () =
    let id = get_thread_id () in
    Mutex.execute tnmutex 
      (fun () -> 
        try
          Printf.sprintf "%d %s" id (Hashtbl.find threadnames id)
        with _ -> 
          Printf.sprintf "%d" id)

  let get_task () =
    match get_task_from_thread () with Some x -> x | None -> ""

	let make_log_message include_time brand priority message =
		let extra =
			Printf.sprintf "%s|%s|%s|%s"
				(get_hostname ())
				(get_thread_name ())
				(get_task ())
				brand in
		Printf.sprintf "[%s%.5s|%s] %s" (if include_time then gettimestring () else "") priority extra message



	let output level priority (fmt: ('a, unit, string, 'b) format4) =
		Printf.kprintf
			(fun s ->
				if not(is_disabled Brand.name level) then begin
					let msg = make_log_message false Brand.name priority s in

					!log level msg
				end
			) fmt
    
	let debug fmt = output `Debug "debug" fmt
	let warn fmt = output `Warning "warn" fmt
	let info fmt = output `Info "info" fmt
	let error fmt = output `Err "error" fmt

	let log_backtrace () =
		let backtrace = Printexc.get_backtrace () in
		debug "%s" (String.escaped backtrace)

	let log_and_ignore_exn f =
		try f () with _ -> log_backtrace ()

end
