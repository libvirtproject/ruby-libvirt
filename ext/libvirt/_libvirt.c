/*
 * libvirt.c: Ruby bindings for libvirt
 *
 * Copyright (C) 2007 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 *
 * Author: David Lutterkort <dlutter@redhat.com>
 */

#include <ruby.h>
#include <libvirt/libvirt.h>
#include "extconf.h"

static VALUE m_libvirt;
static VALUE c_connect;
static VALUE c_domain;
static VALUE c_domain_info;
#if HAVE_TYPE_VIRNETWORKPTR
static VALUE c_network;
#endif
static VALUE c_libvirt_version;
static VALUE c_node_info;
#if HAVE_TYPE_VIRSTORAGEPOOLPTR
static VALUE c_storage_pool;
static VALUE c_storage_pool_info;
#endif
#if HAVE_TYPE_VIRSTORAGEVOLPTR
static VALUE c_storage_vol;
static VALUE c_storage_vol_info;
#endif

/*
 * Internal helpers
 */

/* Macros to ease some of the boilerplate */

#define generic_free(kind, p)                                           \
    do {                                                                \
        int r;                                                          \
        r = vir##kind##Free((vir##kind##Ptr) p);                        \
        if (r < 0)                                                      \
            rb_raise(rb_eSystemCallError, # kind " free failed");       \
    } while(0);

#define generic_get(kind, v)                                            \
    do {                                                                \
        vir##kind##Ptr ptr;                                             \
        Data_Get_Struct(v, vir##kind, ptr);                             \
        if (!ptr)                                                       \
            rb_raise(rb_eArgError, #kind " has been freed");            \
        return ptr;                                                     \
    } while (0);

static VALUE generic_new(VALUE klass, void *ptr, VALUE conn,
                         RUBY_DATA_FUNC free_func) {
    VALUE result;
    result = Data_Wrap_Struct(klass, NULL, free_func, ptr);
    rb_iv_set(result, "@connection", conn);
    return result;
}

/* Connections */
static void connect_close(void *p) {
    int r;

    if (!p)
        return;
    r = virConnectClose((virConnectPtr) p);
    if (r < 0)
        rb_raise(rb_eSystemCallError, "Connection close failed");
}

static VALUE connect_new(virConnectPtr p) {
    return Data_Wrap_Struct(c_connect, NULL, connect_close, p);
}

static virConnectPtr connect_get(VALUE s) {
    generic_get(Connect, s);
}

static VALUE conn_attr(VALUE s) {
    if (rb_obj_is_instance_of(s, c_connect) != Qtrue) {
        s = rb_iv_get(s, "@connection");
    }
    if (rb_obj_is_instance_of(s, c_connect) != Qtrue) {
        rb_raise(rb_eArgError, "Expected Connection object");
    }
    return s;
}

static virConnectPtr conn(VALUE s) {
    s = conn_attr(s);
    virConnectPtr conn;
    Data_Get_Struct(s, virConnect, conn);
    if (!conn)
        rb_raise(rb_eArgError, "Connection has been closed");
    return conn;
}

/* Domains */
static void domain_free(void *d) {
    generic_free(Domain, d);
}

static virDomainPtr domain_get(VALUE s) {
    generic_get(Domain, s);
}

static VALUE domain_new(virDomainPtr d, VALUE conn) {
    return generic_new(c_domain, d, conn, domain_free);
}

/* Network */
#if HAVE_TYPE_VIRNETWORKPTR
static void network_free(void *d) {
    generic_free(Network, d);
}

static virNetworkPtr network_get(VALUE s) {
    generic_get(Network, s);
}

static VALUE network_new(virNetworkPtr n, VALUE conn) {
    return generic_new(c_network, n, conn, network_free);
}
#endif

#if HAVE_TYPE_VIRSTORAGEPOOLPTR
/* StoragePool */
static void pool_free(void *d) {
    generic_free(StoragePool, d);
}

static virStoragePoolPtr pool_get(VALUE s) {
    generic_get(StoragePool, s);
}

static VALUE pool_new(virStoragePoolPtr n, VALUE conn) {
    return generic_new(c_storage_pool, n, conn, pool_free);
}
#endif

/* StorageVol */
#if HAVE_TYPE_VIRSTORAGEVOLPTR
static void vol_free(void *d) {
    generic_free(StorageVol, d);
}
 
static virStorageVolPtr vol_get(VALUE s) {
    generic_get(StorageVol, s);
}
 
static VALUE vol_new(virStorageVolPtr n, VALUE conn) {
    return generic_new(c_storage_vol, n, conn, vol_free);
}
#endif

/* Error handling */
#define _E(cond, conn, fn) \
    do { if (cond) vir_error(conn, fn); } while(0)

NORETURN(static void vir_error(virConnectPtr conn, const char *fn));

static void vir_error(virConnectPtr conn, const char *fn) {
    rb_raise(rb_eSystemCallError, "libvir call %s failed", fn);
}

/*
 * Code generating macros.
 *
 * We only generate function bodies, not the whole function
 * declaration.
 */

/*
 * Generate a call to a virConnectNumOf... function. C is the Ruby VALUE
 * holding the connection and OBJS is a token indicating what objects to
 * get the number of, e.g. 'Domains'
 */
#define gen_conn_num_of(c, objs)                                        \
    do {                                                                \
        int result;                                                     \
        virConnectPtr conn = connect_get(c);                            \
                                                                        \
        result = virConnectNumOf##objs(conn);                           \
        _E(result < 0, conn, "virConnectNumOf" # objs);               \
                                                                        \
        return INT2NUM(result);                                         \
    } while(0)

/*
 * Generate a call to a virConnectList... function. S is the Ruby VALUE
 * holding the connection and OBJS is a token indicating what objects to
 * get the number of, e.g. 'Domains' The list function must return an array
 * of strings, which is returned as a Ruby array
 */
#define gen_conn_list_names(s, objs)                                    \
    do {                                                                \
        int i, r, num;                                                  \
        char **names;                                                   \
        virConnectPtr conn = connect_get(s);                            \
        VALUE result;                                                   \
                                                                        \
        num = virConnectNumOf##objs(conn);                              \
        _E(num < 0, conn, "virConnectNumOf" # objs);                  \
                                                                        \
        names = ALLOC_N(char *, num);                                   \
        r = virConnectList##objs(conn, names, num);                     \
        if (r < 0) {                                                    \
            free(names);                                                \
            _E(r < 0, conn, "virConnectList" # objs);                   \
        }                                                               \
                                                                        \
        result = rb_ary_new2(num);                                      \
        for (i=0; i<num; i++) {                                         \
            rb_ary_push(result, rb_str_new2(names[i]));                 \
            free(names[i]);                                             \
        }                                                               \
        free(names);                                                    \
        return result;                                                  \
    } while(0)

/* Generate a call to a function FUNC which returns an int error, where -1
 * indicates error and 0 success. The Ruby function will return Qnil on
 * success and throw an exception on error.
 */
#define gen_call_void(func, conn, args...)                               \
    do {                                                                \
        int _r_##func;                                                  \
        _r_##func = func(args);                                         \
        _E(_r_##func < 0, conn, #func);                               \
        return Qnil;                                                    \
    } while(0)

/* Generate a call to a function FUNC which returns a string. The Ruby
 * function will return the string on success and throw an exception on
 * error. The string returned by FUNC is freed if dealloc is true.
 */
#define gen_call_string(func, conn, dealloc, args...)                   \
    do {                                                                \
        const char *str;                                                \
        VALUE result;                                                   \
                                                                        \
        str = func(args);                                               \
        _E(str == NULL, conn, # func);                                  \
                                                                        \
        result = rb_str_new2(str);                                      \
        if (dealloc)                                                    \
            free((void *) str);                                         \
        return result;                                                  \
    } while(0)

/* Generate a call to vir##KIND##Free and return Qnil. Set the the embedded
 * vir##KIND##Ptr to NULL. If that pointer is already NULL, do nothing.
 */
#define gen_call_free(kind, s)                                          \
    do {                                                                \
        vir##kind##Ptr ptr;                                             \
        Data_Get_Struct(s, vir##kind, ptr);                             \
        if (ptr != NULL) {                                              \
            int r = vir##kind##Free(ptr);                               \
            _E(r < 0, conn(s), "vir" #kind "Free");                     \
            DATA_PTR(s) = NULL;                                         \
        }                                                               \
        return Qnil;                                                    \
    } while (0)

/*
 * Module Libvirt
 */

/*
 * call-seq:
 *   Libvirt::version(type) -> [ libvirt_version, type_version ]
 *
 * Call
 * +virGetVersion+[http://www.libvirt.org/html/libvirt-libvirt.html#virGetVersion]
 * to get the version of libvirt and of the hypervisor TYPE. Returns an
 * array with two entries of type Libvirt::Version.
 *
 */
VALUE libvirt_version(VALUE m, VALUE t) {
    unsigned long libVer;
    const char *type = NULL;
    unsigned long typeVer;
    int r;
    VALUE result, argv[2];

    type = StringValueCStr(t);
    r = virGetVersion(&libVer, type, &typeVer);
    if (r < 0)
        rb_raise(rb_eArgError, "Failed to get version for %s", type);

    result = rb_ary_new2(2);
    argv[0] = rb_str_new2("libvirt");
    argv[1] = ULONG2NUM(libVer);
    rb_ary_push(result, rb_class_new_instance(2, argv, c_libvirt_version));
    argv[0] = t;
    argv[1] = ULONG2NUM(typeVer);
    rb_ary_push(result, rb_class_new_instance(2, argv, c_libvirt_version));
    return result;
}

/*
 * call-seq:
 *   Libvirt::open(url) -> Libvirt::Connect
 *
 * Open a connection to URL with virConnectOpen[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectOpen]
 */
VALUE libvirt_open(VALUE m, VALUE url) {
    char *str = NULL;

    if (url) {
        str = StringValueCStr(url);
        if (!str)
            rb_raise(rb_eTypeError, "expected string");
    }
    virConnectPtr ptr = virConnectOpen(str);
    if (!ptr)
        rb_raise(rb_eArgError, "Failed to open %s", str);
    return connect_new(ptr);
}

/*
 * call-seq:
 *   Libvirt::openReadOnly(url) -> Libvirt::Connect
 *
 * Open a read-only connection to URL with
 * virConnectOpenReadOnly[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectOpenReadOnly]
 */
VALUE libvirt_open_read_only(VALUE m, VALUE url) {
    char *str = NULL;

    if (url) {
        str = StringValueCStr(url);
        if (!str)
            rb_raise(rb_eTypeError, "expected string");
    }
    virConnectPtr ptr = virConnectOpenReadOnly(str);
    _E(!ptr, NULL, "virConnectOpenReadOnly");

    return connect_new(ptr);
}

/*
 * Class Libvirt::Connect
 */

/*
 * call-seq:
 *   conn.close
 *
 * Close the connection
 */
VALUE libvirt_conn_close(VALUE s) {
    virConnectPtr conn;
    Data_Get_Struct(s, virConnect, conn);
    if (conn) {
        connect_close(conn);
        DATA_PTR(s) = NULL;
    }
    return Qnil;
}

/*
 * call-seq:
 *   conn.closed?
 *
 * Return +true+ if the connection is closed, +false+ if it is open
 */
VALUE libvirt_conn_closed_p(VALUE s) {
    virConnectPtr conn;
    Data_Get_Struct(s, virConnect, conn);
    return (conn==NULL) ? Qtrue : Qfalse;
}

/*
 * call-seq:
 *   conn.type -> string
 *
 * Call +virConnectGetType+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectGetType]
 */
VALUE libvirt_conn_type(VALUE s) {
    gen_call_string(virConnectGetType, connect_get(s), 0,
                    connect_get(s));
}

/*
 * call-seq:
 *   conn.version -> fixnum
 *
 * Call +virConnectGetVersion+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectGetVersion]
 */
VALUE libvirt_conn_version(VALUE s) {
    int r;
    unsigned long v;
    virConnectPtr conn = connect_get(s);

    r = virConnectGetVersion(conn, &v);
    _E(r < 0, conn, "virConnectGetVersion");

    return ULONG2NUM(v);
}

/*
 * call-seq:
 *   conn.hostname -> string
 *
 * Call +virConnectGetHostname+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectGetHostname]
 */
VALUE libvirt_conn_hostname(VALUE s) {
    gen_call_string(virConnectGetHostname, connect_get(s), 1,
                    connect_get(s));
}

/*
 * Call +virConnectGetURI+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectGetURI]
 */
VALUE libvirt_conn_uri(VALUE s) {
    virConnectPtr conn = connect_get(s);
    gen_call_string(virConnectGetURI, conn, 1,
                    conn);
}

/*
 * Call +virConnectGetMaxVcpus+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectGetMaxVcpus]
 */
VALUE libvirt_conn_max_vcpus(VALUE s, VALUE type) {
    int result;
    virConnectPtr conn = connect_get(s);

    result = virConnectGetMaxVcpus(conn, StringValueCStr(type));
    _E(result < 0, conn, "virConnectGetMaxVcpus");

    return INT2NUM(result);
}

/*
 * Call +virNodeInfo+[http://www.libvirt.org/html/libvirt-libvirt.html#virNodeGetInfo]
 */
VALUE libvirt_conn_node_get_info(VALUE s){
    int r;
    virConnectPtr conn = connect_get(s);
    virNodeInfo nodeinfo;
    VALUE result;
    VALUE modelstr;

    r = virNodeGetInfo(conn, &nodeinfo);
    _E(r < 0, conn, "virNodeGetInfo");

    modelstr = rb_str_new2(nodeinfo.model);

    result = rb_class_new_instance(0, NULL, c_node_info);
    rb_iv_set(result, "@model", modelstr);
    rb_iv_set(result, "@memory", ULONG2NUM(nodeinfo.memory));
    rb_iv_set(result, "@cpus", UINT2NUM(nodeinfo.cpus));
    rb_iv_set(result, "@mhz", UINT2NUM(nodeinfo.mhz));
    rb_iv_set(result, "@nodes", UINT2NUM(nodeinfo.nodes));
    rb_iv_set(result, "@sockets", UINT2NUM(nodeinfo.sockets));
    rb_iv_set(result, "@cores", UINT2NUM(nodeinfo.cores));
    rb_iv_set(result, "@threads", UINT2NUM(nodeinfo.threads));
    return result;
}

/*
 * Call +virConnectGetCapabilities+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectGetCapabilities]
 */
VALUE libvirt_conn_capabilities(VALUE s) {
    virConnectPtr conn = connect_get(s);
    gen_call_string(virConnectGetCapabilities, conn, 1,
                    conn);
}

/*
 * Call +virConnectNumOfDomains+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectNumOfDomains]
 */
VALUE libvirt_conn_num_of_domains(VALUE s) {
    gen_conn_num_of(s, Domains);
}

/*
 * Call +virConnectListDomains+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectListDomains]
 */
VALUE libvirt_conn_list_domains(VALUE s) {
    int i, r, num, *ids;
    virConnectPtr conn = connect_get(s);
    VALUE result;

    num = virConnectNumOfDomains(conn);
    _E(num < 0, conn, "virConnectNumOfDomains");

    ids = ALLOC_N(int, num);
    r = virConnectListDomains(conn, ids, num);
    if (r < 0) {
        free(ids);
        _E(r < 0, conn, "virConnectListDomains");
    }

    result = rb_ary_new2(num);
    for (i=0; i<num; i++) {
        rb_ary_push(result, INT2NUM(ids[i]));
    }
    free(ids);
    return result;
}

/*
 * Call +virConnectNumOfDefinedDomains+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectNumOfDefinedDomains]
 */
VALUE libvirt_conn_num_of_defined_domains(VALUE s) {
    gen_conn_num_of(s, DefinedDomains);
}

/*
 * Call +virConnectListDefinedDomains+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectListDefinedDomains]
 */
VALUE libvirt_conn_list_defined_domains(VALUE s) {
    gen_conn_list_names(s, DefinedDomains);
}

/*
 * Call +virConnectNumOfNetworks+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectNumOfNetworks]
 */
VALUE libvirt_conn_num_of_networks(VALUE s) {
    gen_conn_num_of(s, Networks);
}

/*
 * Call +virConnectListNetworks+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectListNetworks]
 */
VALUE libvirt_conn_list_networks(VALUE s) {
    gen_conn_list_names(s, Networks);
}

/*
 * Call +virConnectNumOfDefinedNetworks+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectNumOfDefinedNetworks]
 */
VALUE libvirt_conn_num_of_defined_networks(VALUE s) {
    gen_conn_num_of(s, DefinedNetworks);
}

/*
 * Call +virConnectListDefinedNetworks+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectListDefinedNetworks]
 */
VALUE libvirt_conn_list_defined_networks(VALUE s) {
    gen_conn_list_names(s, DefinedNetworks);
}

#if HAVE_TYPE_VIRSTORAGEPOOLPTR
/*
 * Call +virConnectListStoragePools+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectListStoragePools]
 */
VALUE libvirt_conn_list_storage_pools(VALUE s) {
    gen_conn_list_names(s, StoragePools);
}

/*
 * Call +virConnectNumOfStoragePools+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectNumOfStoragePools]
 */
VALUE libvirt_conn_num_of_storage_pools(VALUE s) {
    gen_conn_num_of(s, StoragePools);
}

/*
 * Call +virConnectListDefinedStoragePools+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectListDefinedStoragePools]
 */
VALUE libvirt_conn_list_defined_storage_pools(VALUE s) {
    gen_conn_list_names(s, DefinedStoragePools);
}

/*
 * Call +virConnectNumOfDefinedStoragePools+[http://www.libvirt.org/html/libvirt-libvirt.html#virConnectNumOfDefinedStoragePools]
 */
VALUE libvirt_conn_num_of_defined_storage_pools(VALUE s) {
    gen_conn_num_of(s, DefinedStoragePools);
}
#endif

/*
 * Class Libvirt::Domain
 */
VALUE libvirt_dom_migrate(VALUE s, VALUE dconn, VALUE flags,
                           VALUE dname, VALUE uri, VALUE bandwidth) {
    rb_raise(rb_eNotImpError, "c_dom_migrate");
}

/*
 * Call +virDomainShutdown+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainShutdown]
 */
VALUE libvirt_dom_shutdown(VALUE s) {
    gen_call_void(virDomainShutdown, conn(s),
                  domain_get(s));
}

/*
 * Call +virDomainReboot+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainReboot]
 */
VALUE libvirt_dom_reboot(int argc, VALUE *argv, VALUE s) {
    VALUE flags;

    rb_scan_args(argc, argv, "01", &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    gen_call_void(virDomainReboot, conn(s), 
                  domain_get(s), NUM2UINT(flags));
}

/*
 * Call +virDomainDestroy+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainDestroy]
 */
VALUE libvirt_dom_destroy(VALUE s) {
    gen_call_void(virDomainDestroy, conn(s),
                  domain_get(s));
}

/*
 * Call +virDomainSuspend+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainSuspend]
 */
VALUE libvirt_dom_suspend(VALUE s) {
    gen_call_void(virDomainSuspend, conn(s),
                  domain_get(s));
}

/*
 * Call +virDomainResume+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainResume]
 */
VALUE libvirt_dom_resume(VALUE s) {
    gen_call_void(virDomainResume, conn(s),
                  domain_get(s));
}

/*
 * Call +virDomainSave+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainSave]
 */
VALUE libvirt_dom_save(VALUE s, VALUE to) {
    gen_call_void(virDomainSave, conn(s),
                  domain_get(s), StringValueCStr(to));
}

/*
 * Call +virDomainCoreDump+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainCoreDump]
 */
VALUE libvirt_dom_core_dump(int argc, VALUE *argv, VALUE s) {
    VALUE to, flags;

    rb_scan_args(argc, argv, "11", &to, &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    gen_call_void(virDomainCoreDump, conn(s),
                  domain_get(s), StringValueCStr(to), NUM2UINT(flags));
}

/*
 * Call +virDomainRestore+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainRestore]
 */
VALUE libvirt_dom_s_restore(VALUE klass, VALUE c, VALUE from) {
    gen_call_void(virDomainRestore, connect_get(c),
                  connect_get(c), StringValueCStr(from));
}

/*
 * call-seq:
 *   domain.info -> Libvirt::Domain::Info
 *
 * Call +virDomainGetInfo+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainGetInfo]
 */
VALUE libvirt_dom_info(VALUE s) {
    virDomainPtr dom = domain_get(s);
    virDomainInfo info;
    int r;
    VALUE result;

    r = virDomainGetInfo(dom, &info);
    _E(r < 0, conn(s), "virDomainGetInfo");

    result = rb_class_new_instance(0, NULL, c_domain_info);
    rb_iv_set(result, "@state", CHR2FIX(info.state));
    rb_iv_set(result, "@max_mem", ULONG2NUM(info.maxMem));
    rb_iv_set(result, "@memory", ULONG2NUM(info.memory));
    rb_iv_set(result, "@nr_virt_cpu", INT2FIX((int) info.nrVirtCpu));
    rb_iv_set(result, "@cpu_time", ULL2NUM(info.cpuTime));
    return result;
}


/*
 * Call +virDomainGetName+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainGetName]
 */
VALUE libvirt_dom_name(VALUE s) {
    gen_call_string(virDomainGetName, conn(s), 0,
                    domain_get(s));
}

/*
 * Call +virDomainGetID+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainGetID]
 */
VALUE libvirt_dom_id(VALUE s) {
    virDomainPtr dom = domain_get(s);
    unsigned int id;

    id = virDomainGetID(dom);
    _E(id < 0, conn(s), "virDomainGetID");

    return UINT2NUM(id);
}

/*
 * Call +virDomainGetUUIDString+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainGetUUIDString]
 */
VALUE libvirt_dom_uuid(VALUE s) {
    virDomainPtr dom = domain_get(s);
    char uuid[VIR_UUID_STRING_BUFLEN];
    int r;

    r = virDomainGetUUIDString(dom, uuid);
    _E(r < 0, conn(s), "virDomainGetUUIDString");

    return rb_str_new2((char *) uuid);
}

/*
 * Call +virDomainGetOSType+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainGetOSType]
 */
VALUE libvirt_dom_os_type(VALUE s) {
    gen_call_string(virDomainGetOSType, conn(s), 1,
                    domain_get(s));
}

/*
 * Call +virDomainGetMaxMemory+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainGetMaxMemory]
 */
VALUE libvirt_dom_max_memory(VALUE s) {
    virDomainPtr dom = domain_get(s);
    unsigned long max_memory;

    max_memory = virDomainGetMaxMemory(dom);
    _E(max_memory == 0, conn(s), "virDomainGetMaxMemory");

    return ULONG2NUM(max_memory);
}

/*
 * Call +virDomainSetMaxMemory+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainSetMaxMemory]
 */
VALUE libvirt_dom_max_memory_set(VALUE s, VALUE max_memory) {
    virDomainPtr dom = domain_get(s);
    int r;

    r = virDomainSetMaxMemory(dom, NUM2ULONG(max_memory));
    _E(r < 0, conn(s), "virDomainSetMaxMemory");

    return ULONG2NUM(max_memory);
}

/*
 * Call +virDomainSetMemory+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainSetMemory]
 */
VALUE libvirt_dom_memory_set(VALUE s, VALUE memory) {
    virDomainPtr dom = domain_get(s);
    int r;

    r = virDomainSetMemory(dom, NUM2ULONG(memory));
    _E(r < 0, conn(s), "virDomainSetMemory");

    return ULONG2NUM(memory);
}

/*
 * Call +virDomainGetMaxVcpus+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainGetMaxVcpus]
 */
VALUE libvirt_dom_max_vcpus(VALUE s) {
    virDomainPtr dom = domain_get(s);
    int vcpus;

    vcpus = virDomainGetMaxVcpus(dom);
    _E(vcpus < 0, conn(s), "virDomainGetMaxVcpus");

    return INT2NUM(vcpus);
}


/*
 * Call +virDomainSetVcpus+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainSetVcpus]
 */
VALUE libvirt_dom_vcpus_set(VALUE s, VALUE nvcpus) {
    virDomainPtr dom = domain_get(s);
    int r;

    r = virDomainSetVcpus(dom, NUM2UINT(nvcpus));
    _E(r < 0, conn(s), "virDomainSetVcpus");

    return r;
}

/*
 * Call +virDomainPinVcpu+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainPinVcpu]
 */
VALUE libvirt_dom_pin_vcpu(VALUE s, VALUE vcpu, VALUE cpulist) {
    virDomainPtr dom = domain_get(s);
    int r, i, len, maplen;
    unsigned char *cpumap;
    virNodeInfo nodeinfo;
    virConnectPtr c = conn(s);

    r = virNodeGetInfo(c, &nodeinfo);
    _E(r < 0, c, "virNodeGetInfo");

    maplen = VIR_CPU_MAPLEN(nodeinfo.cpus);
    cpumap = ALLOC_N(unsigned char, maplen);
    MEMZERO(cpumap, unsigned char, maplen);

    len = RARRAY(cpulist)->len;
    for(i = 0; i < len; i++) {
        VALUE e = rb_ary_entry(cpulist, i);
        VIR_USE_CPU(cpumap, NUM2UINT(e));
    }

    r = virDomainPinVcpu(dom, NUM2UINT(vcpu), cpumap, maplen);
    free(cpumap);
    _E(r < 0, c, "virDomainPinVcpu");

    return r;
}


/*
 * Call +virDomainGetXMLDesc+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainGetXMLDesc]
 */
VALUE libvirt_dom_xml_desc(int argc, VALUE *argv, VALUE s) {
    VALUE flags;

    rb_scan_args(argc, argv, "01", &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    gen_call_string(virDomainGetXMLDesc, conn(s), 1,
                    domain_get(s), 0);
}

/*
 * Call +virDomainUndefine+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainUndefine]
 */
VALUE libvirt_dom_undefine(VALUE s) {
    gen_call_void(virDomainUndefine, conn(s),
                  domain_get(s));
}

/*
 * Call +virDomainCreate+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainCreate]
 */
VALUE libvirt_dom_create(VALUE s) {
    gen_call_void(virDomainCreate, conn(s),
                  domain_get(s));
}

/*
 * Call +virDomainGetAutostart+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainGetAutostart]
 */
VALUE libvirt_dom_autostart(VALUE s){
    virDomainPtr dom = domain_get(s);
    int r, autostart;

    r = virDomainGetAutostart(dom, &autostart);
    _E(r < 0, conn(s), "virDomainAutostart");

    return autostart ? Qtrue : Qfalse;
}

/*
 * Call +virDomainSetAutostart+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainSetAutostart]
 */
VALUE libvirt_dom_autostart_set(VALUE s, VALUE autostart) {
    gen_call_void(virDomainSetAutostart, conn(s),
                  domain_get(s), RTEST(autostart) ? 1 : 0);
}

/*
 * Call +virDomainCreateLinux+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainCreateLinux]
 */
VALUE libvirt_conn_create_linux(int argc, VALUE *argv, VALUE c) {
    virDomainPtr dom;
    virConnectPtr conn = connect_get(c);
    char *xmlDesc;
    VALUE flags, xml;

    rb_scan_args(argc, argv, "11", &xml, &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    xmlDesc = StringValueCStr(xml);

    dom = virDomainCreateLinux(conn, xmlDesc, NUM2UINT(flags));
    _E(dom == NULL, conn, "virDomainCreateLinux");

    return domain_new(dom, c);
}

/*
 * Call +virDomainLookupByName+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainLookupByName]
 */
VALUE libvirt_conn_lookup_domain_by_name(VALUE c, VALUE name) {
    virDomainPtr dom;
    virConnectPtr conn = connect_get(c);

    dom = virDomainLookupByName(conn, StringValueCStr(name));
    _E(dom == NULL, conn, "virDomainLookupByName");

    return domain_new(dom, c);
}

/*
 * Call +virDomainLookupByID+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainLookupByID]
 */
VALUE libvirt_conn_lookup_domain_by_id(VALUE c, VALUE id) {
    virDomainPtr dom;
    virConnectPtr conn = connect_get(c);

    dom = virDomainLookupByID(conn, NUM2INT(id));
    _E(dom == NULL, conn, "virDomainLookupByID");

    return domain_new(dom, c);
}

/*
 * Call +virDomainLookupByUUIDString+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainLookupByUUIDString]
 */
VALUE libvirt_conn_lookup_domain_by_uuid(VALUE c, VALUE uuid) {
    virDomainPtr dom;
    virConnectPtr conn = connect_get(c);

    dom = virDomainLookupByUUIDString(conn, StringValueCStr(uuid));
    _E(dom == NULL, conn, "virDomainLookupByUUID");

    return domain_new(dom, c);
}

/*
 * Call +virDomainDefineXML+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainDefineXML]
 */
VALUE libvirt_conn_define_domain_xml(VALUE c, VALUE xml) {
    virDomainPtr dom;
    virConnectPtr conn = connect_get(c);

    dom = virDomainDefineXML(conn, StringValueCStr(xml));
    _E(dom == NULL, conn, "virDomainDefineXML");

    return domain_new(dom, c);
}

/*
 * Call +virDomainFree+[http://www.libvirt.org/html/libvirt-libvirt.html#virDomainFree]
 */
VALUE libvirt_dom_free(VALUE s) {
    gen_call_free(Domain, s);
}

/*
 * Class Libvirt::Network
 */

#if HAVE_TYPE_VIRNETWORKPTR
/*
 * Call +virNetworkLookupByName+[http://www.libvirt.org/html/libvirt-libvirt.html#virNetworkLookupByName]
 */
VALUE libvirt_conn_lookup_network_by_name(VALUE c, VALUE name) {
    virNetworkPtr netw;
    virConnectPtr conn = connect_get(c);

    netw = virNetworkLookupByName(conn, StringValueCStr(name));
    _E(netw == NULL, conn, "virNetworkLookupByName");

    return network_new(netw, c);
}

/*
 * Call +virNetworkLookupByUUIDString+[http://www.libvirt.org/html/libvirt-libvirt.html#virNetworkLookupByUUIDString]
 */
VALUE libvirt_conn_lookup_network_by_uuid(VALUE c, VALUE uuid) {
    virNetworkPtr netw;
    virConnectPtr conn = connect_get(c);

    netw = virNetworkLookupByUUIDString(conn, StringValueCStr(uuid));
    _E(netw == NULL, conn, "virNetworkLookupByUUID");

    return network_new(netw, c);
}

/*
 * Call +virNetworkCreateXML+[http://www.libvirt.org/html/libvirt-libvirt.html#virNetworkCreateXML]
 */
VALUE libvirt_conn_create_network_xml(VALUE c, VALUE xml) {
    virNetworkPtr netw;
    virConnectPtr conn = connect_get(c);
    char *xmlDesc;

    xmlDesc = StringValueCStr(xml);

    netw = virNetworkCreateXML(conn, xmlDesc);
    _E(netw == NULL, conn, "virNetworkCreateXML");

    return network_new(netw, c);
}

/*
 * Call +virNetworkDefineXML+[http://www.libvirt.org/html/libvirt-libvirt.html#virNetworkDefineXML]
 */
VALUE libvirt_conn_define_network_xml(VALUE c, VALUE xml) {
    virNetworkPtr netw;
    virConnectPtr conn = connect_get(c);

    netw = virNetworkDefineXML(conn, StringValueCStr(xml));
    _E(netw == NULL, conn, "virNetworkDefineXML");

    return network_new(netw, c);
}
#endif

#if HAVE_TYPE_VIRNETWORKPTR
/*
 * Call +virNetworkUndefine+[http://www.libvirt.org/html/libvirt-libvirt.html#virNetworkUndefine]
 */
VALUE libvirt_netw_undefine(VALUE s) {
    gen_call_void(virNetworkUndefine, conn(s),
                  network_get(s));
}

/*
 * Call +virNetworkCreate+[http://www.libvirt.org/html/libvirt-libvirt.html#virNetworkCreate]
 */
VALUE libvirt_netw_create(VALUE s) {
    gen_call_void(virNetworkCreate, conn(s),
                  network_get(s));
}

/*
 * Call +virNetworkDestroy+[http://www.libvirt.org/html/libvirt-libvirt.html#virNetworkDestroy]
 */
VALUE libvirt_netw_destroy(VALUE s) {
    gen_call_void(virNetworkDestroy, conn(s),
                  network_get(s));
}

/*
 * Call +virNetworkGetName+[http://www.libvirt.org/html/libvirt-libvirt.html#virNetworkGetName]
 */
VALUE libvirt_netw_name(VALUE s) {
    gen_call_string(virNetworkGetName, conn(s), 0,
                    network_get(s));
}

/*
 * Call +virNetworkGetUUIDString+[http://www.libvirt.org/html/libvirt-libvirt.html#virNetworkGetUUIDString]
 */
VALUE libvirt_netw_uuid(VALUE s) {
    virNetworkPtr netw = network_get(s);
    char uuid[VIR_UUID_STRING_BUFLEN];
    int r;

    r = virNetworkGetUUIDString(netw, uuid);
    _E(r < 0, conn(s), "virNetworkGetUUIDString");

    return rb_str_new2((char *) uuid);
}

/*
 * Call +virNetworkGetXMLDesc+[http://www.libvirt.org/html/libvirt-libvirt.html#virNetworkGetXMLDesc]
 */
VALUE libvirt_netw_xml_desc(int argc, VALUE *argv, VALUE s) {
    VALUE flags;

    rb_scan_args(argc, argv, "01", &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    gen_call_string(virNetworkGetXMLDesc, conn(s), 1,
                    network_get(s), NUM2UINT(flags));
}

/*
 * Call +virNetworkGetBridgeName+[http://www.libvirt.org/html/libvirt-libvirt.html#virNetworkGetBridgeName]
 */
VALUE libvirt_netw_bridge_name(VALUE s) {
    gen_call_string(virNetworkGetBridgeName, conn(s), 1,
                    network_get(s));
}

/*
 * Call +virNetworkGetAutostart+[http://www.libvirt.org/html/libvirt-libvirt.html#virNetworkGetAutostart]
 */
VALUE libvirt_netw_autostart(VALUE s){
    virNetworkPtr netw = network_get(s);
    int r, autostart;

    r = virNetworkGetAutostart(netw, &autostart);
    _E(r < 0, conn(s), "virNetworkAutostart");

    return autostart ? Qtrue : Qfalse;
}

/*
 * Call +virNetworkSetAutostart+[http://www.libvirt.org/html/libvirt-libvirt.html#virNetworkSetAutostart]
 */
VALUE libvirt_netw_autostart_set(VALUE s, VALUE autostart) {
    gen_call_void(virNetworkSetAutostart, conn(s),
                  network_get(s), RTEST(autostart) ? 1 : 0);
}

/*
 * Call +virNetworkFree+[http://www.libvirt.org/html/libvirt-libvirt.html#virNetworkFree]
 */
VALUE libvirt_netw_free(VALUE s) {
    gen_call_free(Network, s);
}
#endif

/*
 * Libvirt::StoragePool
 */

#if HAVE_TYPE_VIRSTORAGEPOOLPTR
/*
 * Call +virStoragePoolLookupByName+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolLookupByName]
 */
VALUE libvirt_conn_lookup_pool_by_name(VALUE c, VALUE name) {
    virStoragePoolPtr pool;
    virConnectPtr conn = connect_get(c);

    pool = virStoragePoolLookupByName(conn, StringValueCStr(name));
    _E(pool == NULL, conn, "virStoragePoolLookupByName");

    return pool_new(pool, c);
}

/*
 * Call +virStoragePoolLookupByUUIDString+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolLookupByUUIDString]
 */
VALUE libvirt_conn_lookup_pool_by_uuid(VALUE c, VALUE uuid) {
    virStoragePoolPtr pool;
    virConnectPtr conn = connect_get(c);

    pool = virStoragePoolLookupByUUIDString(conn, StringValueCStr(uuid));
    _E(pool == NULL, conn, "virStoragePoolLookupByUUID");

    return pool_new(pool, c);
}

/*
 * Call +virStoragePoolLookupByVolume+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolLookupByVolume]
 */
VALUE libvirt_vol_get_pool(VALUE v) {
    virStoragePoolPtr pool;

    pool = virStoragePoolLookupByVolume(vol_get(v));
    _E(pool == NULL, conn(v), "virStoragePoolLookupByVolume");

    return pool_new(pool, conn_attr(v));
}

/*
 * Call +virStoragePoolCreateXML+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolCreateXML]
 */
VALUE libvirt_conn_create_pool_xml(int argc, VALUE *argv, VALUE c) {
    virStoragePoolPtr pool;
    virConnectPtr conn = connect_get(c);
    char *xmlDesc;
    VALUE xml, flags;

    rb_scan_args(argc, argv, "11", &xml, &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    xmlDesc = StringValueCStr(xml);

    pool = virStoragePoolCreateXML(conn, xmlDesc, NUM2UINT(flags));
    _E(pool == NULL, conn, "virStoragePoolCreateXML");

    return pool_new(pool, c);
}

/*
 * Call +virStoragePoolDefineXML+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolDefineXML]
 */
VALUE libvirt_conn_define_pool_xml(int argc, VALUE *argv, VALUE c) {
    virStoragePoolPtr pool;
    virConnectPtr conn = connect_get(c);
    VALUE xml, flags;

    rb_scan_args(argc, argv, "11", &xml, &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    pool = virStoragePoolDefineXML(conn, StringValueCStr(xml), NUM2UINT(flags));
    _E(pool == NULL, conn, "virStoragePoolDefineXML");

    return pool_new(pool, c);
}

/*
 * Call +virStoragePoolBuild+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolBuild]
 */
VALUE libvirt_pool_build(int argc, VALUE *argv, VALUE p) {
    VALUE flags;

    rb_scan_args(argc, argv, "01", &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    gen_call_void(virStoragePoolBuild, conn(p),
                  pool_get(p), NUM2UINT(flags));
}

/*
 * Call +virStoragePoolUndefine+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolUndefine]
 */
VALUE libvirt_pool_undefine(VALUE p) {
    gen_call_void(virStoragePoolUndefine, conn(p),
                  pool_get(p));
}

/*
 * Call +virStoragePoolCreate+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolCreate]
 */
VALUE libvirt_pool_create(int argc, VALUE *argv, VALUE p) {
    VALUE flags;

    rb_scan_args(argc, argv, "01", &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    gen_call_void(virStoragePoolCreate, conn(p),
                  pool_get(p), NUM2UINT(flags));
}

/*
 * Call +virStoragePoolDestroy+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolDestroy]
 */
VALUE libvirt_pool_destroy(VALUE p) {
    gen_call_void(virStoragePoolDestroy, conn(p),
                  pool_get(p));
}

/*
 * Call +virStoragePoolDelete+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolDelete]
 */
VALUE libvirt_pool_delete(int argc, VALUE *argv, VALUE p) {
    VALUE flags;

    rb_scan_args(argc, argv, "01", &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    gen_call_void(virStoragePoolDelete, conn(p),
                  pool_get(p), NUM2UINT(flags));
}

/*
 * Call +virStoragePoolRefresh+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolRefresh]
 */
VALUE libvirt_pool_refresh(int argc, VALUE *argv, VALUE p) {
    VALUE flags;

    rb_scan_args(argc, argv, "01", &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    gen_call_void(virStoragePoolRefresh, conn(p),
                  pool_get(p), NUM2UINT(flags));
}

/*
 * Call +virStoragePoolGetName+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolGetName]
 */
VALUE libvirt_pool_name(VALUE s) {
    const char *name;

    name = virStoragePoolGetName(pool_get(s));
    _E(name == NULL, conn(s), "virStoragePoolGetName");

    return rb_str_new2(name);
}

/*
 * Call +virStoragePoolGetUUIDString+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolGetUUIDString]
 */
VALUE libvirt_pool_uuid(VALUE s) {
    char uuid[VIR_UUID_STRING_BUFLEN];
    int r;

    r = virStoragePoolGetUUIDString(pool_get(s), uuid);
    _E(r < 0, conn(s), "virStoragePoolGetUUIDString");

    return rb_str_new2((char *) uuid);
}

/*
 * Call +virStoragePoolGetInfo+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolGetInfo]
 */
VALUE libvirt_pool_info(VALUE s) {
    virStoragePoolInfo info;
    int r;
    VALUE result;

    r = virStoragePoolGetInfo(pool_get(s), &info);
    _E(r < 0, conn(s), "virStoragePoolGetInfo");

    result = rb_class_new_instance(0, NULL, c_storage_pool_info);
    rb_iv_set(result, "@state", INT2FIX(info.state));
    rb_iv_set(result, "@capacity", ULL2NUM(info.capacity));
    rb_iv_set(result, "@allocation", ULL2NUM(info.allocation));
    rb_iv_set(result, "@available", ULL2NUM(info.available));

    return result;
}

/*
 * Call +virStoragePoolGetXMLDesc+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolGetXMLDesc]
 */
VALUE libvirt_pool_xml_desc(int argc, VALUE *argv, VALUE s) {
    VALUE flags;

    rb_scan_args(argc, argv, "01", &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    gen_call_string(virStoragePoolGetXMLDesc, conn(s), 1,
                    pool_get(s), NUM2UINT(flags));
}

/*
 * Call +virStoragePoolGetAutostart+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolGetAutostart]
 */
VALUE libvirt_pool_autostart(VALUE s){
    int r, autostart;

    r = virStoragePoolGetAutostart(pool_get(s), &autostart);
    _E(r < 0, conn(s), "virStoragePoolGetAutostart");

    return autostart ? Qtrue : Qfalse;
}

/*
 * Call +virStoragePoolSetAutostart+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolSetAutostart]
 */
VALUE libvirt_pool_autostart_set(VALUE s, VALUE autostart) {
    gen_call_void(virStoragePoolSetAutostart, conn(s),
                  pool_get(s), RTEST(autostart) ? 1 : 0);
}

/*
 * Call +virStoragePoolNumOfVolumes+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolNumOfVolumes]
 */
VALUE libvirt_pool_num_of_volumes(VALUE s) {
    int n = virStoragePoolNumOfVolumes(pool_get(s));
    _E(n < 0, conn(s), "virStoragePoolNumOfVolumes");

    return INT2FIX(n);
}

/*
 * Call +virStoragePoolListVolumes+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolListVolumes]
 */
VALUE libvirt_pool_list_volumes(VALUE s) {
    int i, r, num;
    char **names;
    virStoragePoolPtr pool = pool_get(s);
    VALUE result;

    num = virStoragePoolNumOfVolumes(pool);
    _E(num < 0, conn(s), "virStoragePoolNumOfVolumes");

    names = ALLOC_N(char *, num);
    r = virStoragePoolListVolumes(pool, names, num);
    if (r < 0) {
        free(names);
        _E(r < 0, conn(s), "virStoragePoolListVolumes");
    }

    result = rb_ary_new2(num);
    for (i=0; i<num; i++) {
        rb_ary_push(result, rb_str_new2(names[i]));
        // FIXME: Should these really be freed ?
        free(names[i]);
    }
    free(names);
    return result;
}

/*
 * Call +virStoragePoolFree+[http://www.libvirt.org/html/libvirt-libvirt.html#virStoragePoolFree]
 */
VALUE libvirt_pool_free(VALUE s) {
    gen_call_free(StoragePool, s);
}
#endif

/*
 * Libvirt::StorageVol
 */
#if HAVE_TYPE_VIRSTORAGEVOLPTR
/*
 * Call +virStorageVolLookupByName+[http://www.libvirt.org/html/libvirt-libvirt.html#virStorageVolLookupByName]
 */
VALUE libvirt_pool_lookup_vol_by_name(VALUE p, VALUE name) {
    virStorageVolPtr vol;

    vol = virStorageVolLookupByName(pool_get(p), StringValueCStr(name));
    _E(vol == NULL, conn(p), "virStorageVolLookupByName");

    return vol_new(vol, conn_attr(p));
}

/*
 * Call +virStorageVolLookupByKey+[http://www.libvirt.org/html/libvirt-libvirt.html#virStorageVolLookupByKey]
 */
VALUE libvirt_pool_lookup_vol_by_key(VALUE p, VALUE key) {
    virStorageVolPtr vol;

    // FIXME: Why does this take a connection, not a pool ?
    vol = virStorageVolLookupByKey(conn(p), StringValueCStr(key));
    _E(vol == NULL, conn(p), "virStorageVolLookupByKey");

    return vol_new(vol, conn_attr(p));
}

/*
 * Call +virStorageVolLookupByPath+[http://www.libvirt.org/html/libvirt-libvirt.html#virStorageVolLookupByPath]
 */
VALUE libvirt_pool_lookup_vol_by_path(VALUE p, VALUE path) {
    virStorageVolPtr vol;

    // FIXME: Why does this take a connection, not a pool ?
    vol = virStorageVolLookupByPath(conn(p), StringValueCStr(path));
    _E(vol == NULL, conn(p), "virStorageVolLookupByPath");

    return vol_new(vol, conn_attr(p));
}

/*
 * Call +virStorageVolGetName+[http://www.libvirt.org/html/libvirt-libvirt.html#virStorageVolGetName]
 */
VALUE libvirt_vol_name(VALUE v) {
    gen_call_string(virStorageVolGetName, conn(v), 0,
                    vol_get(v));
}

/*
 * Call +virStorageVolGetKey+[http://www.libvirt.org/html/libvirt-libvirt.html#virStorageVolGetKey]
 */
VALUE libvirt_vol_key(VALUE v) {
    gen_call_string(virStorageVolGetKey, conn(v), 0,
                    vol_get(v));
}

/*
 * Call +virStorageVolCreateXML+[http://www.libvirt.org/html/libvirt-libvirt.html#virStorageVolCreateXML]
 */
VALUE libvirt_pool_vol_create_xml(int argc, VALUE *argv, VALUE p) {
    virStorageVolPtr vol;
    virConnectPtr c = conn(p);
    char *xmlDesc;
    VALUE xml, flags;

    rb_scan_args(argc, argv, "11", &xml, &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    xmlDesc = StringValueCStr(xml);

    vol = virStorageVolCreateXML(pool_get(p), xmlDesc, NUM2UINT(flags));
    _E(vol == NULL, c, "virNetworkCreateXML");

    return vol_new(vol, conn_attr(p));
}

/*
 * Call +virStorageVolDelete+[http://www.libvirt.org/html/libvirt-libvirt.html#virStorageVolDelete]
 */
VALUE libvirt_vol_delete(int argc, VALUE *argv, VALUE v) {
    VALUE flags;

    rb_scan_args(argc, argv, "01", &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    gen_call_void(virStorageVolDelete, conn(v),
                  vol_get(v), NUM2UINT(flags));
}

/*
 * Call +virStorageVolGetInfo+[http://www.libvirt.org/html/libvirt-libvirt.html#virStorageVolGetInfo]
 */
VALUE libvirt_vol_info(VALUE v) {
    virStorageVolInfo info;
    int r;
    VALUE result;

    r = virStorageVolGetInfo(vol_get(v), &info);
    _E(r < 0, conn(v), "virStorageVolGetInfo");

    result = rb_class_new_instance(0, NULL, c_storage_vol_info);
    rb_iv_set(result, "@type", INT2NUM(info.type));
    rb_iv_set(result, "@capacity", ULL2NUM(info.capacity));
    rb_iv_set(result, "@allocation", ULL2NUM(info.allocation));

    return result;
}

/*
 * Call +virStorageVolGetXMLDesc+[http://www.libvirt.org/html/libvirt-libvirt.html#virStorageVolGetXMLDesc]
 */
VALUE libvirt_vol_xml_desc(int argc, VALUE *argv, VALUE v) {
    VALUE flags;

    rb_scan_args(argc, argv, "01", &flags);

    if (NIL_P(flags))
        flags = INT2FIX(0);

    gen_call_string(virStorageVolGetXMLDesc, conn(v), 1,
                    vol_get(v), NUM2UINT(flags));
}

/*
 * Call +virStorageVolGetPath+[http://www.libvirt.org/html/libvirt-libvirt.html#virStorageVolGetPath]
 */
VALUE libvirt_vol_path(VALUE v) {
    gen_call_string(virStorageVolGetPath, conn(v), 1,
                    vol_get(v));
}

/*
 * Call +virStorageVolFree+[http://www.libvirt.org/html/libvirt-libvirt.html#virStorageVolFree]
 */
VALUE libvirt_vol_free(VALUE s) {
    gen_call_free(StorageVol, s);
}
#endif

static void init_storage(void) {
    /*
     * Class Libvirt::StoragePool and Libvirt::StoragePoolInfo
     */
#if HAVE_TYPE_VIRSTORAGEPOOLPTR
    c_storage_pool_info = rb_define_class_under(m_libvirt, "StoragePoolInfo",
                                                rb_cObject);
    rb_define_attr(c_storage_pool_info, "state", 1, 0);
    rb_define_attr(c_storage_pool_info, "capacity", 1, 0);
    rb_define_attr(c_storage_pool_info, "allocation", 1, 0);
    rb_define_attr(c_storage_pool_info, "available", 1, 0);

    c_storage_pool = rb_define_class_under(m_libvirt, "StoragePool", 
                                           rb_cObject);
#define DEF_POOLCONST(name)                                        \
    rb_define_const(c_storage_pool, #name, INT2NUM(VIR_STORAGE_POOL_##name))
    /* virStoragePoolState */
    DEF_POOLCONST(INACTIVE);
    DEF_POOLCONST(BUILDING);
    DEF_POOLCONST(RUNNING);
    DEF_POOLCONST(DEGRADED);
    /* virStoragePoolBuildFlags */
    DEF_POOLCONST(BUILD_NEW);
    DEF_POOLCONST(BUILD_REPAIR);
    DEF_POOLCONST(BUILD_RESIZE);
    /* virStoragePoolDeleteFlags */
    DEF_POOLCONST(DELETE_NORMAL);
    DEF_POOLCONST(DELETE_ZEROED);
#undef DEF_POOLCONST
    /* Creating/destroying pools */
    rb_define_method(c_storage_pool, "build", libvirt_pool_build, -1);
    rb_define_method(c_storage_pool, "undefine", libvirt_pool_undefine, 0);
    rb_define_method(c_storage_pool, "create", libvirt_pool_create, -1);
    rb_define_method(c_storage_pool, "destroy", libvirt_pool_destroy, 0);
    rb_define_method(c_storage_pool, "delete", libvirt_pool_delete, -1);
    rb_define_method(c_storage_pool, "refresh", libvirt_pool_refresh, -1);
    /* StoragePool information */
    rb_define_method(c_storage_pool, "name", libvirt_pool_name, 0);
    rb_define_method(c_storage_pool, "uuid", libvirt_pool_uuid, 0);
    rb_define_method(c_storage_pool, "info", libvirt_pool_info, 0);
    rb_define_method(c_storage_pool, "xml_desc", libvirt_pool_xml_desc, -1);
    rb_define_method(c_storage_pool, "autostart", libvirt_pool_autostart, 0);
    rb_define_method(c_storage_pool, "autostart=",
                     libvirt_pool_autostart_set, 1);
    /* List/lookup storage volumes within a pool */
    rb_define_method(c_storage_pool, "num_of_volumes",
                     libvirt_pool_num_of_volumes, 0);
    rb_define_method(c_storage_pool, "list_volumes",
                     libvirt_pool_list_volumes, 0);
    /* Lookup volumes based on various attributes */
    rb_define_method(c_storage_pool, "lookup_volume_by_name",
                     libvirt_pool_lookup_vol_by_name, 1);
    rb_define_method(c_storage_pool, "lookup_volume_by_key",
                     libvirt_pool_lookup_vol_by_key, 1);
    rb_define_method(c_storage_pool, "lookup_volume_by_path",
                     libvirt_pool_lookup_vol_by_path, 1);
    rb_define_method(c_storage_pool, "free", libvirt_pool_free, 0);
    rb_define_method(c_storage_pool, "create_vol_xml", libvirt_pool_vol_create_xml, -1);
#endif

#if HAVE_TYPE_VIRSTORAGEVOLPTR
    /*
     * Class Libvirt::StorageVol and Libvirt::StorageVolInfo
     */
    c_storage_vol_info = rb_define_class_under(m_libvirt, "StorageVolInfo",
                                               rb_cObject);
    rb_define_attr(c_storage_vol_info, "type", 1, 0);
    rb_define_attr(c_storage_vol_info, "capacity", 1, 0);
    rb_define_attr(c_storage_vol_info, "allocation", 1, 0);

    c_storage_vol = rb_define_class_under(m_libvirt, "StorageVol",
                                          rb_cObject);
#define DEF_VOLCONST(name)                                        \
    rb_define_const(c_storage_vol, #name, INT2NUM(VIR_STORAGE_VOL_##name))
    /* virStorageVolType */
    DEF_VOLCONST(FILE);
    DEF_VOLCONST(BLOCK);
    /* virStorageVolDeleteFlags */
    DEF_VOLCONST(DELETE_NORMAL);
    DEF_VOLCONST(DELETE_ZEROED);
#undef DEF_VOLCONST

    rb_define_method(c_storage_vol, "pool", libvirt_vol_get_pool, 0);
    rb_define_method(c_storage_vol, "name", libvirt_vol_name, 0);
    rb_define_method(c_storage_vol, "key", libvirt_vol_key, 0);
    rb_define_method(c_storage_vol, "delete", libvirt_vol_delete, -1);
    rb_define_method(c_storage_vol, "info", libvirt_vol_info, 0);
    rb_define_method(c_storage_vol, "xml_desc", libvirt_vol_xml_desc, -1);
    rb_define_method(c_storage_vol, "path", libvirt_vol_path, 0);
    rb_define_method(c_storage_vol, "free", libvirt_vol_free, 0);
#endif
}

void Init__libvirt() {
    int r;

    m_libvirt = rb_define_module("Libvirt");
    c_libvirt_version = rb_define_class_under(m_libvirt, "Version",
                                              rb_cObject);

    /*
     * Class Libvirt::Connect
     */
    c_connect = rb_define_class_under(m_libvirt, "Connect", rb_cObject);

    rb_define_module_function(m_libvirt, "version", libvirt_version, 1);
	rb_define_module_function(m_libvirt, "open", libvirt_open, 1);
	rb_define_module_function(m_libvirt, "open_read_only",
                              libvirt_open_read_only, 1);

    rb_define_method(c_connect, "close", libvirt_conn_close, 0);
    rb_define_method(c_connect, "closed?", libvirt_conn_closed_p, 0);
    rb_define_method(c_connect, "type", libvirt_conn_type, 0);
    rb_define_method(c_connect, "version", libvirt_conn_version, 0);
    rb_define_method(c_connect, "hostname", libvirt_conn_hostname, 0);
    rb_define_method(c_connect, "uri", libvirt_conn_uri, 0);
    rb_define_method(c_connect, "max_vcpus", libvirt_conn_max_vcpus, 1);
    rb_define_method(c_connect, "node_get_info", libvirt_conn_node_get_info, 0);
    rb_define_method(c_connect, "capabilities", libvirt_conn_capabilities, 0);
    rb_define_method(c_connect, "num_of_domains", libvirt_conn_num_of_domains, 0);
    rb_define_method(c_connect, "list_domains", libvirt_conn_list_domains, 0);
    rb_define_method(c_connect, "num_of_defined_domains",
                     libvirt_conn_num_of_defined_domains, 0);
    rb_define_method(c_connect, "list_defined_domains",
                     libvirt_conn_list_defined_domains, 0);
#if HAVE_TYPE_VIRNETWORKPTR
    rb_define_method(c_connect, "num_of_networks",
                     libvirt_conn_num_of_networks, 0);
    rb_define_method(c_connect, "list_networks", libvirt_conn_list_networks, 0);
    rb_define_method(c_connect, "num_of_defined_networks",
                     libvirt_conn_num_of_defined_networks, 0);
    rb_define_method(c_connect, "list_defined_networks",
                     libvirt_conn_list_defined_networks, 0);
#endif
#if HAVE_TYPE_VIRSTORAGEPOOLPTR
    rb_define_method(c_connect, "num_of_storage_pools",
                     libvirt_conn_num_of_storage_pools, 0);
    rb_define_method(c_connect, "list_storage_pools",
                     libvirt_conn_list_storage_pools, 0);
    rb_define_method(c_connect, "num_of_defined_storage_pools",
                     libvirt_conn_num_of_defined_storage_pools, 0);
    rb_define_method(c_connect, "list_defined_storage_pools",
                     libvirt_conn_list_defined_storage_pools, 0);
#endif
    // Domain creation/lookup
    rb_define_method(c_connect, "create_domain_linux",
                     libvirt_conn_create_linux, -1);
    rb_define_method(c_connect, "lookup_domain_by_name",
                     libvirt_conn_lookup_domain_by_name, 1);
    rb_define_method(c_connect, "lookup_domain_by_id",
                     libvirt_conn_lookup_domain_by_id, 1);
    rb_define_method(c_connect, "lookup_domain_by_uuid",
                     libvirt_conn_lookup_domain_by_uuid, 1);
    rb_define_method(c_connect, "define_domain_xml",
                     libvirt_conn_define_domain_xml, 1);
    // Network creation/lookup
#if HAVE_TYPE_VIRNETWORKPTR
    rb_define_method(c_connect, "lookup_network_by_name",
                     libvirt_conn_lookup_network_by_name, 1);
    rb_define_method(c_connect, "lookup_network_by_uuid",
                     libvirt_conn_lookup_network_by_uuid, 1);
    rb_define_method(c_connect, "create_network_xml",
                     libvirt_conn_create_network_xml, 1);
    rb_define_method(c_connect, "define_network_xml",
                     libvirt_conn_define_network_xml, 1);
#endif
    // Storage pool creation/lookup
#if HAVE_TYPE_VIRSTORAGEPOOLPTR
    rb_define_method(c_connect, "lookup_storage_pool_by_name",
                     libvirt_conn_lookup_pool_by_name, 1);
    rb_define_method(c_connect, "lookup_storage_pool_by_uuid",
                     libvirt_conn_lookup_pool_by_uuid, 1);
    rb_define_method(c_connect, "create_storage_pool_xml",
                     libvirt_conn_create_pool_xml, -1);
    rb_define_method(c_connect, "define_storage_pool_xml",
                     libvirt_conn_define_pool_xml, -1);
#endif

    /*
     * Class Libvirt::Connect::Nodeinfo
     */
    c_node_info = rb_define_class_under(c_connect, "Nodeinfo", rb_cObject);
    rb_define_attr(c_node_info, "model", 1, 0);
    rb_define_attr(c_node_info, "memory", 1, 0);
    rb_define_attr(c_node_info, "cpus", 1, 0);
    rb_define_attr(c_node_info, "mhz", 1, 0);
    rb_define_attr(c_node_info, "nodes", 1, 0);
    rb_define_attr(c_node_info, "sockets", 1, 0);
    rb_define_attr(c_node_info, "cores", 1, 0);
    rb_define_attr(c_node_info, "threads", 1, 0);

    /*
     * Class Libvirt::Domain
     */
    c_domain = rb_define_class_under(m_libvirt, "Domain", rb_cObject);
#define DEF_DOMSTATE(name) \
    rb_define_const(c_domain, #name, INT2NUM(VIR_DOMAIN_##name))
    /* virDomainState */
    DEF_DOMSTATE(NOSTATE);
    DEF_DOMSTATE(RUNNING);
    DEF_DOMSTATE(BLOCKED);
    DEF_DOMSTATE(PAUSED);
    DEF_DOMSTATE(SHUTDOWN);
    DEF_DOMSTATE(SHUTOFF);
    DEF_DOMSTATE(CRASHED);
#undef DEF_DOMSTATE

    rb_define_method(c_domain, "migrate", libvirt_dom_migrate, 5);
    rb_define_attr(c_domain, "connection", 1, 0);
    rb_define_method(c_domain, "shutdown", libvirt_dom_shutdown, 0);
    rb_define_method(c_domain, "reboot", libvirt_dom_reboot, -1);
    rb_define_method(c_domain, "destroy", libvirt_dom_destroy, 0);
    rb_define_method(c_domain, "suspend", libvirt_dom_suspend, 0);
    rb_define_method(c_domain, "resume", libvirt_dom_resume, 0);
    rb_define_method(c_domain, "save", libvirt_dom_save, 1);
    rb_define_singleton_method(c_domain, "restore", libvirt_dom_s_restore, 2);
    rb_define_method(c_domain, "core_dump", libvirt_dom_core_dump, -1);
    rb_define_method(c_domain, "info", libvirt_dom_info, 0);
    rb_define_method(c_domain, "name", libvirt_dom_name, 0);
    rb_define_method(c_domain, "id", libvirt_dom_id, 0);
    rb_define_method(c_domain, "uuid", libvirt_dom_uuid, 0);
    rb_define_method(c_domain, "os_type", libvirt_dom_os_type, 0);
    rb_define_method(c_domain, "max_memory", libvirt_dom_max_memory, 0);
    rb_define_method(c_domain, "max_memory=", libvirt_dom_max_memory_set, 1);
    rb_define_method(c_domain, "memory=", libvirt_dom_memory_set, 1);
    rb_define_method(c_domain, "max_vcpus", libvirt_dom_max_vcpus, 0);
    rb_define_method(c_domain, "vcpus=", libvirt_dom_vcpus_set, 1);
    rb_define_method(c_domain, "pin_vcpu", libvirt_dom_pin_vcpu, 2);
    rb_define_method(c_domain, "xml_desc", libvirt_dom_xml_desc, -1);
    rb_define_method(c_domain, "undefine", libvirt_dom_undefine, 0);
    rb_define_method(c_domain, "create", libvirt_dom_create, 0);
    rb_define_method(c_domain, "autostart", libvirt_dom_autostart, 0);
    rb_define_method(c_domain, "autostart=", libvirt_dom_autostart_set, 1);
    rb_define_method(c_domain, "free", libvirt_dom_free, 0);

    /*
     * Class Libvirt::Domain::Info
     */
    c_domain_info = rb_define_class_under(c_domain, "Info", rb_cObject);
    rb_define_attr(c_domain_info, "state", 1, 0);
    rb_define_attr(c_domain_info, "max_mem", 1, 0);
    rb_define_attr(c_domain_info, "memory", 1, 0);
    rb_define_attr(c_domain_info, "nr_virt_cpu", 1, 0);
    rb_define_attr(c_domain_info, "cpu_time", 1, 0);

    /*
     * Class Libvirt::Network
     */
#if HAVE_TYPE_VIRNETWORKPTR
    c_network = rb_define_class_under(m_libvirt, "Network", rb_cObject);
    rb_define_attr(c_network, "connection", 1, 0);
    rb_define_method(c_network, "undefine", libvirt_netw_undefine, 0);
    rb_define_method(c_network, "create", libvirt_netw_create, 0);
    rb_define_method(c_network, "destroy", libvirt_netw_destroy, 0);
    rb_define_method(c_network, "name", libvirt_netw_name, 0);
    rb_define_method(c_network, "uuid", libvirt_netw_uuid, 0);
    rb_define_method(c_network, "xml_desc", libvirt_netw_xml_desc, -1);
    rb_define_method(c_network, "bridge_name", libvirt_netw_bridge_name, 0);
    rb_define_method(c_network, "autostart", libvirt_netw_autostart, 0);
    rb_define_method(c_network, "autostart=", libvirt_netw_autostart_set, 1);
    rb_define_method(c_network, "free", libvirt_netw_free, 0);
#endif

    init_storage();

    r = virInitialize();
    if (r < 0)
        rb_raise(rb_eSystemCallError, "virInitialize failed");
}

/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
