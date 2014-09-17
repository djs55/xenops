/*
 * Copyright (C) 2014 Citrix Systems Inc.
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
 */
/*
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <xenctrl.h>
#include <xenguest.h>
#include "xc_dom.h"

#include <caml/mlvalues.h>
#include <caml/memory.h>
#include <caml/alloc.h>
#include <caml/custom.h>
#include <caml/fail.h>
#include <caml/callback.h>

/* Encapsulation of opaque window handles (of type WINDOW *)
   as OCaml custom blocks. */

static struct custom_operations dom_ops = {
  "org.xen.xapi-project.dom",
  custom_finalize_default,
  custom_compare_default,
  custom_hash_default,
  custom_serialize_default,
  custom_deserialize_default
};

#define Dom_val(v) (*((struct xc_dom_image **) Data_custom_val(v)))

static value alloc_dom(struct xc_dom_image *c_dom)
{
    value v = alloc_custom(&dom_ops, sizeof(struct xc_dom_image *), 0, 1);
    Dom_val(v) = c_dom;
    return v;
}

/* matches xenctrl_stubs.c */
#define _H(__h) ((struct xc_interface *)(__h))

extern struct xc_dom_image *xc_dom_allocate(xc_interface *xch, const char *cmdline, const char *features);

value stub_xc_dom_allocate(value xc, value pv_cmdline, value features)
{
    CAMLparam3(xc, pv_cmdline, features);

    struct xc_dom_image *c_dom;
    const char *c_pv_cmdline = String_val(pv_cmdline);
    const char *c_features = String_val(features);
    struct xc_interface *c_xc = _H(xc);

    c_dom = xc_dom_allocate(c_xc, c_pv_cmdline, c_features);
    if (!c_dom) caml_failwith("xc_dom_allocate failed");

    CAMLreturn(alloc_dom(c_dom));
}

value stub_set_pvh_enabled(value dom, value flag)
{
    CAMLparam2(dom, flag);
    struct xc_dom_image *c_dom = Dom_val(dom);

    c_dom->pvh_enabled = Bool_val(flag);
    CAMLreturn(Val_unit);
}


