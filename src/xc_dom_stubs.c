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
#include <libxl.h>
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
#define _H(__h) ((xc_interface *)(__h))


value stub_xc_dom_allocate(value xc, value pv_cmdline, value features)
{
    CAMLparam3(xc, pv_cmdline, features);

    struct xc_dom_image *c_dom;
    const char *c_pv_cmdline = String_val(pv_cmdline);
    const char *c_features = String_val(features);
    xc_interface *c_xc = _H(xc);

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

value stub_xc_dom_kernel_mem(value dom, value v_cstruct)
{
    CAMLparam2(dom, v_cstruct);
    CAMLlocal3(v_ba, v_ofs, v_len);
    struct xc_dom_image *c_dom = Dom_val(dom);
    int ret;
    v_ba = Field(v_cstruct, 0);
    v_ofs = Field(v_cstruct, 1);
    v_len = Field(v_cstruct, 2);

    ret = xc_dom_kernel_mem(c_dom, Caml_ba_data_val(v_ba) + Int_val(v_ofs), Int_val(v_len));
    if ( ret != 0) caml_failwith("xc_dom_kernel_mem");

    CAMLreturn(Val_unit);
}

#define SET(field, cast)                              \
value stub_set_##field(value dom, value x)            \
{                                                     \
    CAMLparam2(dom, x);                               \
    struct xc_dom_image *c_dom = Dom_val(dom);        \
    c_dom->field = cast(x);                           \
    CAMLreturn(Val_unit);                             \
}

SET(flags, Int_val)
SET(console_evtchn, Int_val)
SET(console_domid, Int_val)
SET(xenstore_evtchn, Int_val)
SET(xenstore_domid, Int_val)
SET(claim_enabled, Bool_val)

value stub_xc_dom_boot_xen_init(value xc, value dom, value domid)
{
    CAMLparam3(xc, dom, domid);
    struct xc_dom_image *c_dom = Dom_val(dom);
    xc_interface *c_xc = _H(xc);

    if (xc_dom_boot_xen_init(c_dom, c_xc, Int_val(domid)) != 0)
        caml_failwith("xc_dom_boot_xen_init");
    
    CAMLreturn(Val_unit);
}

value stub_xc_dom_rambase_init(value dom)
{
    CAMLparam1(dom);
    struct xc_dom_image *c_dom = Dom_val(dom);
#ifdef __arm__
    if (xc_dom_rambase_init(c_dom, GUEST_RAM_BASE) != 0 )
        caml_failwith("xc_dom_rambase");
#endif
    CAMLreturn(Val_unit);
}

#define DO(fn)                                    \
value stub_##fn(value dom)                        \
{                                                 \
    CAMLparam1(dom);                              \
    struct xc_dom_image *c_dom = Dom_val(dom);    \
    if (fn(c_dom) != 0)                           \
        caml_failwith("fn");                      \
    CAMLreturn(Val_unit);                         \
}

DO(xc_dom_parse_image);
DO(xc_dom_boot_mem_init);
DO(xc_dom_build_image);
DO(xc_dom_boot_image);
DO(xc_dom_gnttab_init);

#define libxl__gc void
extern
int libxl__arch_domain_init_hw_description(libxl__gc *gc,
                                           libxl_domain_build_info *info,
                                           struct xc_dom_image *dom);

static libxl_ctx *get_ctx() {
    int ret;
    static libxl_ctx *ctx = NULL;

    if (!ctx) {
      ret = libxl_ctx_alloc(&ctx, 0, 0, NULL);
      if (ret != 0) caml_failwith("libxl_ctx_alloc");
    }
    return ctx;
}

static libxl__gc get_gc(){
    libxl_ctx *ctx = get_ctx();
    return (void*) ctx + sizeof(void*) * 3;
}

value stub_libxl__arch_domain_init_hw_description(value dom)
{
    CAMLparam1(dom);
    struct xc_dom_image *c_dom = Dom_val(dom);
    libxl_domain_build_info info;
    info.type = LIBXL_DOMAIN_TYPE_PV;
    info.max_vcpus = 1;
    info.cmdline = NULL;
    libxl__gc *gc = get_gc();

    if (libxl__arch_domain_init_hw_description(gc, &info, c_dom) != 0)
        caml_failwith("libxl__arch_domain_init_hw_description");

    CAMLreturn(Val_unit);
}

extern
int libxl__arch_domain_finalise_hw_description(libxl__gc *gc,
                                               libxl_domain_build_info *info,
                                               struct xc_dom_image *dom)

value stub_libxl__arch_domain_finalise_hw_description(value dom)
{
    CAMLparam1(dom);
    struct xc_dom_image *c_dom = Dom_val(dom);
    libxl_domain_build_info info;
    info->ramdisk = NULL;
    libxl__gc *gc = get_gc();

    if (libxl__arch_domain_finalise_hw_description(gc, &info, c_dom) != 0)
        caml_failwith("libxl__arch_domain_finalise_hw_description");

    CAMLreturn(Val_unit);
}
