/*
 * Copyright © 2009 CNRS
 * Copyright © 2009-2016 Inria.  All rights reserved.
 * Copyright © 2009-2011 Université Bordeaux
 * Copyright © 2009-2011 Cisco Systems, Inc.  All rights reserved.
 * See COPYING in top-level directory.
 */

#include <private/autogen/config.h>
#include <hwloc.h>
#include <private/xml.h>
#include <private/private.h>
#include <private/misc.h>
#include <private/debug.h>

#include <math.h>

int
hwloc__xml_verbose(void)
{
  static int first = 1;
  static int verbose = 0;
  if (first) {
    const char *env = getenv("HWLOC_XML_VERBOSE");
    if (env)
      verbose = atoi(env);
    first = 0;
  }
  return verbose;
}

static int
hwloc_nolibxml_import(void)
{
  static int first = 1;
  static int nolibxml = 0;
  if (first) {
    const char *env = getenv("HWLOC_NO_LIBXML_IMPORT");
    if (env)
      nolibxml = atoi(env);
    first = 0;
  }
  return nolibxml;
}

static int
hwloc_nolibxml_export(void)
{
  static int first = 1;
  static int nolibxml = 0;
  if (first) {
    const char *env = getenv("HWLOC_NO_LIBXML_EXPORT");
    if (env)
      nolibxml = atoi(env);
    first = 0;
  }
  return nolibxml;
}

#define BASE64_ENCODED_LENGTH(length) (4*(((length)+2)/3))

/*********************************
 ********* XML callbacks *********
 *********************************/

/* set when registering nolibxml and libxml components.
 * modifications protected by the components mutex.
 * read by the common XML code in topology-xml.c to jump to the right XML backend.
 */
static struct hwloc_xml_callbacks *hwloc_nolibxml_callbacks = NULL, *hwloc_libxml_callbacks = NULL;

void
hwloc_xml_callbacks_register(struct hwloc_xml_component *comp)
{
  if (!hwloc_nolibxml_callbacks)
    hwloc_nolibxml_callbacks = comp->nolibxml_callbacks;
  if (!hwloc_libxml_callbacks)
    hwloc_libxml_callbacks = comp->libxml_callbacks;
}

void
hwloc_xml_callbacks_reset(void)
{
  hwloc_nolibxml_callbacks = NULL;
  hwloc_libxml_callbacks = NULL;
}

/************************************************
 ********* XML import (common routines) *********
 ************************************************/

#define _HWLOC_OBJ_CACHE_OLD HWLOC_OBJ_L5CACHE /* temporarily used when importing pre-v2.0 attribute-less cache types */

static void
hwloc__xml_import_object_attr(struct hwloc_topology *topology, struct hwloc_obj *obj,
			      const char *name, const char *value,
			      hwloc__xml_import_state_t state)
{
  if (!strcmp(name, "type")) {
    /* already handled */
    return;
  }

  else if (!strcmp(name, "os_level"))
    { /* ignored since v2.0 but still allowed for backward compat with v1.10 */ }
  else if (!strcmp(name, "os_index"))
    obj->os_index = strtoul(value, NULL, 10);
  else if (!strcmp(name, "gp_index")) {
    obj->gp_index = strtoull(value, NULL, 10);
    if (!obj->gp_index && hwloc__xml_verbose())
      fprintf(stderr, "%s: unexpected zero gp_index, topology may be invalid\n", state->global->msgprefix);
    if (obj->gp_index >= topology->next_gp_index)
      topology->next_gp_index = obj->gp_index + 1;
  } else if (!strcmp(name, "cpuset")) {
    obj->cpuset = hwloc_bitmap_alloc();
    hwloc_bitmap_sscanf(obj->cpuset, value);
  } else if (!strcmp(name, "complete_cpuset")) {
    obj->complete_cpuset = hwloc_bitmap_alloc();
    hwloc_bitmap_sscanf(obj->complete_cpuset,value);
  } else if (!strcmp(name, "online_cpuset")) {
    { /* ignored since v2.0 but still allowed for backward compat with v1.10 */ }
  } else if (!strcmp(name, "allowed_cpuset")) {
    obj->allowed_cpuset = hwloc_bitmap_alloc();
    hwloc_bitmap_sscanf(obj->allowed_cpuset, value);
  } else if (!strcmp(name, "nodeset")) {
    obj->nodeset = hwloc_bitmap_alloc();
    hwloc_bitmap_sscanf(obj->nodeset, value);
  } else if (!strcmp(name, "complete_nodeset")) {
    obj->complete_nodeset = hwloc_bitmap_alloc();
    hwloc_bitmap_sscanf(obj->complete_nodeset, value);
  } else if (!strcmp(name, "allowed_nodeset")) {
    obj->allowed_nodeset = hwloc_bitmap_alloc();
    hwloc_bitmap_sscanf(obj->allowed_nodeset, value);
  } else if (!strcmp(name, "name"))
    obj->name = strdup(value);
  else if (!strcmp(name, "subtype"))
    obj->subtype = strdup(value);

  else if (!strcmp(name, "cache_size")) {
    unsigned long long lvalue = strtoull(value, NULL, 10);
    if (hwloc_obj_type_is_cache(obj->type))
      obj->attr->cache.size = lvalue;
    else if (hwloc__xml_verbose())
      fprintf(stderr, "%s: ignoring cache_size attribute for non-cache object type\n",
	      state->global->msgprefix);
  }

  else if (!strcmp(name, "cache_linesize")) {
    unsigned long lvalue = strtoul(value, NULL, 10);
    if (hwloc_obj_type_is_cache(obj->type))
      obj->attr->cache.linesize = lvalue;
    else if (hwloc__xml_verbose())
      fprintf(stderr, "%s: ignoring cache_linesize attribute for non-cache object type\n",
	      state->global->msgprefix);
  }

  else if (!strcmp(name, "cache_associativity")) {
    unsigned long lvalue = strtoul(value, NULL, 10);
    if (hwloc_obj_type_is_cache(obj->type))
      obj->attr->cache.associativity = lvalue;
    else if (hwloc__xml_verbose())
      fprintf(stderr, "%s: ignoring cache_associativity attribute for non-cache object type\n",
	      state->global->msgprefix);
  }

  else if (!strcmp(name, "cache_type")) {
    unsigned long lvalue = strtoul(value, NULL, 10);
    if (hwloc_obj_type_is_cache(obj->type)) {
      if (lvalue == HWLOC_OBJ_CACHE_UNIFIED
	  || lvalue == HWLOC_OBJ_CACHE_DATA
	  || lvalue == HWLOC_OBJ_CACHE_INSTRUCTION)
	obj->attr->cache.type = (hwloc_obj_cache_type_t) lvalue;
      else
	fprintf(stderr, "%s: ignoring invalid cache_type attribute %ld\n",
		state->global->msgprefix, lvalue);
    } else if (hwloc__xml_verbose())
      fprintf(stderr, "%s: ignoring cache_type attribute for non-cache object type\n",
	      state->global->msgprefix);
  }

  else if (!strcmp(name, "local_memory"))
    obj->memory.local_memory = strtoull(value, NULL, 10);

  else if (!strcmp(name, "depth")) {
    unsigned long lvalue = strtoul(value, NULL, 10);
    switch (obj->type) {
      case HWLOC_OBJ_L1CACHE:
      case HWLOC_OBJ_L2CACHE:
      case HWLOC_OBJ_L3CACHE:
      case HWLOC_OBJ_L4CACHE:
      case HWLOC_OBJ_L5CACHE:
      case HWLOC_OBJ_L1ICACHE:
      case HWLOC_OBJ_L2ICACHE:
      case HWLOC_OBJ_L3ICACHE:
	obj->attr->cache.depth = lvalue;
	break;
      case HWLOC_OBJ_GROUP:
	/* will be overwritten by the core */
	break;
      case HWLOC_OBJ_BRIDGE:
	/* will be overwritten by the core */
	break;
      default:
	if (hwloc__xml_verbose())
	  fprintf(stderr, "%s: ignoring depth attribute for object type without depth\n",
		  state->global->msgprefix);
	break;
    }
  }

  else if (!strcmp(name, "kind")) {
    unsigned long lvalue = strtoul(value, NULL, 10);
    if (obj->type == HWLOC_OBJ_GROUP)
      obj->attr->group.kind = lvalue;
    else if (hwloc__xml_verbose())
      fprintf(stderr, "%s: ignoring kind attribute for non-group object type\n",
	      state->global->msgprefix);
  }

  else if (!strcmp(name, "subkind")) {
    unsigned long lvalue = strtoul(value, NULL, 10);
    if (obj->type == HWLOC_OBJ_GROUP)
      obj->attr->group.subkind = lvalue;
    else if (hwloc__xml_verbose())
      fprintf(stderr, "%s: ignoring subkind attribute for non-group object type\n",
	      state->global->msgprefix);
  }

  else if (!strcmp(name, "pci_busid")) {
    switch (obj->type) {
    case HWLOC_OBJ_PCI_DEVICE:
    case HWLOC_OBJ_BRIDGE: {
      unsigned domain, bus, dev, func;
      if (sscanf(value, "%04x:%02x:%02x.%01x",
		 &domain, &bus, &dev, &func) != 4) {
	if (hwloc__xml_verbose())
	  fprintf(stderr, "%s: ignoring invalid pci_busid format string %s\n",
		  state->global->msgprefix, value);
      } else {
	obj->attr->pcidev.domain = domain;
	obj->attr->pcidev.bus = bus;
	obj->attr->pcidev.dev = dev;
	obj->attr->pcidev.func = func;
      }
      break;
    }
    default:
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: ignoring pci_busid attribute for non-PCI object\n",
		state->global->msgprefix);
      break;
    }
  }

  else if (!strcmp(name, "pci_type")) {
    switch (obj->type) {
    case HWLOC_OBJ_PCI_DEVICE:
    case HWLOC_OBJ_BRIDGE: {
      unsigned classid, vendor, device, subvendor, subdevice, revision;
      if (sscanf(value, "%04x [%04x:%04x] [%04x:%04x] %02x",
		 &classid, &vendor, &device, &subvendor, &subdevice, &revision) != 6) {
	if (hwloc__xml_verbose())
	  fprintf(stderr, "%s: ignoring invalid pci_type format string %s\n",
		  state->global->msgprefix, value);
      } else {
	obj->attr->pcidev.class_id = classid;
	obj->attr->pcidev.vendor_id = vendor;
	obj->attr->pcidev.device_id = device;
	obj->attr->pcidev.subvendor_id = subvendor;
	obj->attr->pcidev.subdevice_id = subdevice;
	obj->attr->pcidev.revision = revision;
      }
      break;
    }
    default:
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: ignoring pci_type attribute for non-PCI object\n",
		state->global->msgprefix);
      break;
    }
  }

  else if (!strcmp(name, "pci_link_speed")) {
    switch (obj->type) {
    case HWLOC_OBJ_PCI_DEVICE:
    case HWLOC_OBJ_BRIDGE: {
      obj->attr->pcidev.linkspeed = (float) atof(value);
      break;
    }
    default:
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: ignoring pci_link_speed attribute for non-PCI object\n",
		state->global->msgprefix);
      break;
    }
  }

  else if (!strcmp(name, "bridge_type")) {
    switch (obj->type) {
    case HWLOC_OBJ_BRIDGE: {
      unsigned upstream_type, downstream_type;
      if (sscanf(value, "%u-%u", &upstream_type, &downstream_type) != 2) {
	if (hwloc__xml_verbose())
	  fprintf(stderr, "%s: ignoring invalid bridge_type format string %s\n",
		  state->global->msgprefix, value);
      } else {
	obj->attr->bridge.upstream_type = (hwloc_obj_bridge_type_t) upstream_type;
	obj->attr->bridge.downstream_type = (hwloc_obj_bridge_type_t) downstream_type;
      };
      break;
    }
    default:
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: ignoring bridge_type attribute for non-bridge object\n",
		state->global->msgprefix);
      break;
    }
  }

  else if (!strcmp(name, "bridge_pci")) {
    switch (obj->type) {
    case HWLOC_OBJ_BRIDGE: {
      unsigned domain, secbus, subbus;
      if (sscanf(value, "%04x:[%02x-%02x]",
		 &domain, &secbus, &subbus) != 3) {
	if (hwloc__xml_verbose())
	  fprintf(stderr, "%s: ignoring invalid bridge_pci format string %s\n",
		  state->global->msgprefix, value);
      } else {
	obj->attr->bridge.downstream.pci.domain = domain;
	obj->attr->bridge.downstream.pci.secondary_bus = secbus;
	obj->attr->bridge.downstream.pci.subordinate_bus = subbus;
      }
      break;
    }
    default:
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: ignoring bridge_pci attribute for non-bridge object\n",
		state->global->msgprefix);
      break;
    }
  }

  else if (!strcmp(name, "osdev_type")) {
    switch (obj->type) {
    case HWLOC_OBJ_OS_DEVICE: {
      unsigned osdev_type;
      if (sscanf(value, "%u", &osdev_type) != 1) {
	if (hwloc__xml_verbose())
	  fprintf(stderr, "%s: ignoring invalid osdev_type format string %s\n",
		  state->global->msgprefix, value);
      } else
	obj->attr->osdev.type = (hwloc_obj_osdev_type_t) osdev_type;
      break;
    }
    default:
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: ignoring osdev_type attribute for non-osdev object\n",
		state->global->msgprefix);
      break;
    }
  }




  /*************************
   * deprecated (from 1.0)
   */
  else if (!strcmp(name, "dmi_board_vendor")) {
    hwloc_obj_add_info(obj, "DMIBoardVendor", value);
  }
  else if (!strcmp(name, "dmi_board_name")) {
    hwloc_obj_add_info(obj, "DMIBoardName", value);
  }

  /*************************
   * deprecated (from 0.9)
   */
  else if (!strcmp(name, "memory_kB")) {
    unsigned long long lvalue = strtoull(value, NULL, 10);
    switch (obj->type) {
      case _HWLOC_OBJ_CACHE_OLD:
	obj->attr->cache.size = lvalue << 10;
	break;
      case HWLOC_OBJ_NUMANODE:
      case HWLOC_OBJ_MACHINE:
      case HWLOC_OBJ_SYSTEM:
	obj->memory.local_memory = lvalue << 10;
	break;
      default:
	if (hwloc__xml_verbose())
	  fprintf(stderr, "%s: ignoring memory_kB attribute for object type without memory\n",
		  state->global->msgprefix);
	break;
    }
  }
  else if (!strcmp(name, "huge_page_size_kB")) {
    unsigned long lvalue = strtoul(value, NULL, 10);
    switch (obj->type) {
      case HWLOC_OBJ_NUMANODE:
      case HWLOC_OBJ_MACHINE:
      case HWLOC_OBJ_SYSTEM:
	if (!obj->memory.page_types) {
	  obj->memory.page_types = malloc(sizeof(*obj->memory.page_types));
	  obj->memory.page_types_len = 1;
	}
	obj->memory.page_types[0].size = lvalue << 10;
	break;
      default:
	if (hwloc__xml_verbose())
	  fprintf(stderr, "%s: ignoring huge_page_size_kB attribute for object type without huge pages\n",
		  state->global->msgprefix);
	break;
    }
  }
  else if (!strcmp(name, "huge_page_free")) {
    unsigned long lvalue = strtoul(value, NULL, 10);
    switch (obj->type) {
      case HWLOC_OBJ_NUMANODE:
      case HWLOC_OBJ_MACHINE:
      case HWLOC_OBJ_SYSTEM:
	if (!obj->memory.page_types) {
	  obj->memory.page_types = malloc(sizeof(*obj->memory.page_types));
	  obj->memory.page_types_len = 1;
	}
	obj->memory.page_types[0].count = lvalue;
	break;
      default:
	if (hwloc__xml_verbose())
	  fprintf(stderr, "%s: ignoring huge_page_free attribute for object type without huge pages\n",
		  state->global->msgprefix);
	break;
    }
  }
  /*
   * end of deprecated (from 0.9)
   *******************************/



  else if (hwloc__xml_verbose())
    fprintf(stderr, "%s: ignoring unknown object attribute %s\n",
	    state->global->msgprefix, name);
}


static int
hwloc__xml_import_info(hwloc_topology_t topology __hwloc_attribute_unused, hwloc_obj_t obj,
		       hwloc__xml_import_state_t state)
{
  char *infoname = NULL;
  char *infovalue = NULL;

  while (1) {
    char *attrname, *attrvalue;
    if (state->global->next_attr(state, &attrname, &attrvalue) < 0)
      break;
    if (!strcmp(attrname, "name"))
      infoname = attrvalue;
    else if (!strcmp(attrname, "value"))
      infovalue = attrvalue;
    else
      return -1;
  }

  if (infoname) {
    /* empty strings are ignored by libxml */
    if (!strcmp(infoname, "Type") || !strcmp(infoname, "CoProcType")) {
      if (infovalue)
	obj->subtype = strdup(infovalue);
    } else {
      hwloc_obj_add_info(obj, infoname, infovalue ? infovalue : "");
    }
  }

  return state->global->close_tag(state);
}

static int
hwloc__xml_import_pagetype(hwloc_topology_t topology __hwloc_attribute_unused, hwloc_obj_t obj,
			   hwloc__xml_import_state_t state)
{
  uint64_t size = 0, count = 0;

  while (1) {
    char *attrname, *attrvalue;
    if (state->global->next_attr(state, &attrname, &attrvalue) < 0)
      break;
    if (!strcmp(attrname, "size"))
      size = strtoull(attrvalue, NULL, 10);
    else if (!strcmp(attrname, "count"))
      count = strtoull(attrvalue, NULL, 10);
    else
      return -1;
  }

  if (size) {
    int idx = obj->memory.page_types_len;
    struct hwloc_obj_memory_page_type_s *tmp;
    tmp = realloc(obj->memory.page_types, (idx+1)*sizeof(*obj->memory.page_types));
    if (tmp) { /* if failed to allocate, ignore this page_type entry */
      obj->memory.page_types = tmp;
      obj->memory.page_types_len = idx+1;
      obj->memory.page_types[idx].size = size;
      obj->memory.page_types[idx].count = count;
    }
  }

  return state->global->close_tag(state);
}

static int
hwloc__xml_import_v1distances(struct hwloc_xml_backend_data_s *data,
			      hwloc_obj_t obj,
			      hwloc__xml_import_state_t state)
{
  unsigned long reldepth = 0, nbobjs = 0;
  float latbase = 0;
  char *tag;
  int ret;

  while (1) {
    char *attrname, *attrvalue;
    if (state->global->next_attr(state, &attrname, &attrvalue) < 0)
      break;
    if (!strcmp(attrname, "nbobjs"))
      nbobjs = strtoul(attrvalue, NULL, 10);
    else if (!strcmp(attrname, "relative_depth"))
      reldepth = strtoul(attrvalue, NULL, 10);
    else if (!strcmp(attrname, "latency_base"))
      latbase = (float) atof(attrvalue);
    else
      return -1;
  }

  if (nbobjs && reldepth && latbase) {
    unsigned i;
    float *matrix;
    struct hwloc__xml_imported_v1distances_s *v1dist;

    matrix = malloc(nbobjs*nbobjs*sizeof(float));
    v1dist = malloc(sizeof(*v1dist));
    if (!matrix || !v1dist) {
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: failed to allocate v1distance matrix for %lu objects\n",
		state->global->msgprefix, nbobjs);
      free(v1dist);
      free(matrix);
      return -1;
    }

    v1dist->kind = HWLOC_DISTANCES_KIND_FROM_OS|HWLOC_DISTANCES_KIND_MEANS_LATENCY;
    /* TODO: we can't know for sure if it comes from the OS.
     * On Linux/x86, it would be 10 on the diagonal.
     * On Solaris/T5, 15 on the diagonal.
     * Just check whether all values are integers, and that all values on the diagonal are minimal and identical?
     */

    v1dist->nbobjs = nbobjs;
    v1dist->floats = matrix;

    for(i=0; i<nbobjs*nbobjs; i++) {
      struct hwloc__xml_import_state_s childstate;
      char *attrname, *attrvalue;
      float val;

      ret = state->global->find_child(state, &childstate, &tag);
      if (ret <= 0 || strcmp(tag, "latency")) {
	/* a latency child is needed */
	free(matrix);
	free(v1dist);
	return -1;
      }

      ret = state->global->next_attr(&childstate, &attrname, &attrvalue);
      if (ret < 0 || strcmp(attrname, "value")) {
	free(matrix);
	free(v1dist);
	return -1;
      }

      val = (float) atof((char *) attrvalue);
      matrix[i] = val * latbase;

      ret = state->global->close_tag(&childstate);
      if (ret < 0)
	return -1;

      state->global->close_child(&childstate);
    }

    if (nbobjs < 2) {
      /* distances with a single object are useless, even if the XML isn't invalid */
      assert(nbobjs == 1);
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: ignoring invalid distance matrix with only 1 object\n",
		state->global->msgprefix);
      free(matrix);
      free(v1dist);

    } else if (obj->parent) {
      /* we currently only import distances attached to root.
       * we can't save obj in v1dist because obj could be dropped during insert if ignored.
       * we could save its complete_cpu/nodeset instead to find it back later.
       * but it doesn't matter much since only NUMA distances attached to root matter.
       */
      free(matrix);
      free(v1dist);

    } else {
      /* queue the distance for real */
      v1dist->prev = data->last_v1dist;
      v1dist->next = NULL;
      if (data->last_v1dist)
	data->last_v1dist->next = v1dist;
      else
	data->first_v1dist = v1dist;
      data->last_v1dist = v1dist;
    }
  }

  return state->global->close_tag(state);
}

static int
hwloc__xml_import_userdata(hwloc_topology_t topology __hwloc_attribute_unused, hwloc_obj_t obj,
			   hwloc__xml_import_state_t state)
{
  size_t length = 0;
  int encoded = 0;
  char *name = NULL; /* optional */
  int ret;

  while (1) {
    char *attrname, *attrvalue;
    if (state->global->next_attr(state, &attrname, &attrvalue) < 0)
      break;
    if (!strcmp(attrname, "length"))
      length = strtoul(attrvalue, NULL, 10);
    else if (!strcmp(attrname, "encoding"))
      encoded = !strcmp(attrvalue, "base64");
    else if (!strcmp(attrname, "name"))
      name = attrvalue;
    else
      return -1;
  }

  if (!topology->userdata_import_cb) {
    char *buffer;
    size_t reallength = encoded ? BASE64_ENCODED_LENGTH(length) : length;
    ret = state->global->get_content(state, &buffer, reallength);
    if (ret < 0)
      return -1;

  } else if (topology->userdata_not_decoded) {
      char *buffer, *fakename;
      size_t reallength = encoded ? BASE64_ENCODED_LENGTH(length) : length;
      ret = state->global->get_content(state, &buffer, reallength);
      if (ret < 0)
        return -1;
      fakename = malloc(6 + 1 + (name ? strlen(name) : 4) + 1);
      if (!fakename)
	return -1;
      sprintf(fakename, encoded ? "base64%c%s" : "normal%c%s", name ? ':' : '-', name ? name : "anon");
      topology->userdata_import_cb(topology, obj, fakename, buffer, length);
      free(fakename);

  } else if (encoded && length) {
      char *encoded_buffer;
      size_t encoded_length = BASE64_ENCODED_LENGTH(length);
      ret = state->global->get_content(state, &encoded_buffer, encoded_length);
      if (ret < 0)
        return -1;
      if (ret) {
	char *decoded_buffer = malloc(length+1);
	if (!decoded_buffer)
	  return -1;
	assert(encoded_buffer[encoded_length] == 0);
	ret = hwloc_decode_from_base64(encoded_buffer, decoded_buffer, length+1);
	if (ret != (int) length) {
	  free(decoded_buffer);
	  return -1;
	}
	topology->userdata_import_cb(topology, obj, name, decoded_buffer, length);
	free(decoded_buffer);
      }

  } else { /* always handle length==0 in the non-encoded case */
      char *buffer = "";
      if (length) {
	ret = state->global->get_content(state, &buffer, length);
	if (ret < 0)
	  return -1;
      }
      topology->userdata_import_cb(topology, obj, name, buffer, length);
  }

  state->global->close_content(state);
  return state->global->close_tag(state);
}

static void hwloc__xml_import_report_outoforder(hwloc_topology_t topology, hwloc_obj_t new, hwloc_obj_t old)
{
  char *progname = hwloc_progname(topology);
  const char *origversion = hwloc_obj_get_info_by_name(topology->levels[0][0], "hwlocVersion");
  const char *origprogname = hwloc_obj_get_info_by_name(topology->levels[0][0], "ProcessName");
  char *c1, *cc1, t1[64];
  char *c2 = NULL, *cc2 = NULL, t2[64];

  hwloc_bitmap_asprintf(&c1, new->cpuset);
  hwloc_bitmap_asprintf(&cc1, new->complete_cpuset);
  hwloc_obj_type_snprintf(t1, sizeof(t1), new, 0);

  if (old->cpuset)
    hwloc_bitmap_asprintf(&c2, old->cpuset);
  if (old->complete_cpuset)
    hwloc_bitmap_asprintf(&cc2, old->complete_cpuset);
  hwloc_obj_type_snprintf(t2, sizeof(t2), old, 0);

  fprintf(stderr, "****************************************************************************\n");
  fprintf(stderr, "* hwloc has encountered an out-of-order XML topology load.\n");
  fprintf(stderr, "* Object %s cpuset %s complete %s\n",
	  t1, c1, cc1);
  fprintf(stderr, "* was inserted after object %s with %s and %s.\n",
	  t2, c2 ? c2 : "none", cc2 ? cc2 : "none");
  fprintf(stderr, "* The error occured in hwloc %s inside process `%s', while\n",
	  HWLOC_VERSION,
	  progname ? progname : "<unknown>");
  if (origversion || origprogname)
    fprintf(stderr, "* the input XML was generated by hwloc %s inside process `%s'.\n",
	    origversion ? origversion : "(unknown version)",
	    origprogname ? origprogname : "<unknown>");
  else
    fprintf(stderr, "* the input XML was generated by an unspecified ancient hwloc release.\n");
  fprintf(stderr, "* Please check that your input topology XML file is valid.\n");
  fprintf(stderr, "* Set HWLOC_DEBUG_CHECK=1 in the environment to detect further issues.\n");
  fprintf(stderr, "****************************************************************************\n");

  free(c1);
  free(cc1);
  free(c2);
  free(cc2);
  free(progname);
}

static int
hwloc__xml_import_object(hwloc_topology_t topology,
			 struct hwloc_xml_backend_data_s *data,
			 hwloc_obj_t parent, hwloc_obj_t obj, int *gotignored,
			 hwloc__xml_import_state_t state)
{
  int ignored = 0;
  int childrengotignored = 0;
  int attribute_less_cache = 0;

  /* process attributes */
  while (1) {
    char *attrname, *attrvalue;
    if (state->global->next_attr(state, &attrname, &attrvalue) < 0)
      break;
    if (!strcmp(attrname, "type")) {
      if (hwloc_type_sscanf(attrvalue, &obj->type, NULL, 0) < 0) {
	if (!strcasecmp(attrvalue, "Cache")) {
	  obj->type = _HWLOC_OBJ_CACHE_OLD; /* will be fixed below */
	  attribute_less_cache = 1;
	} else
	  goto error_with_object;
      }
    } else {
      /* type needed first */
      if (obj->type == HWLOC_OBJ_TYPE_NONE)
	goto error_with_object;
      hwloc__xml_import_object_attr(topology, obj, attrname, attrvalue, state);
    }
  }

  /* fixup attribute-less caches imported from pre-v2.0 XMLs */
  if (attribute_less_cache) {
    assert(obj->type == _HWLOC_OBJ_CACHE_OLD);
    obj->type = hwloc_cache_type_by_depth_type(obj->attr->cache.depth, obj->attr->cache.type);
  }

  /* check that cache attributes are coherent with the actual type */
  if (hwloc_obj_type_is_cache(obj->type)
      && obj->type != hwloc_cache_type_by_depth_type(obj->attr->cache.depth, obj->attr->cache.type)) {
    if (hwloc__xml_verbose())
      fprintf(stderr, "invalid cache type %s with attribute depth %u and type %d\n",
	      hwloc_type_name(obj->type), obj->attr->cache.depth, obj->attr->cache.type);
    goto error_with_object;
  }

  /* fixup Misc objects inserted by cpusets in pre-v2.0 XMLs */
  if (obj->type == HWLOC_OBJ_MISC && obj->cpuset)
    obj->type = HWLOC_OBJ_GROUP;

  /* check special types vs cpuset */
  if (!obj->cpuset && !hwloc_obj_type_is_special(obj->type)) {
    if (hwloc__xml_verbose())
      fprintf(stderr, "invalid normal object %s P#%u without cpuset\n",
	      hwloc_type_name(obj->type), obj->os_index);
    goto error_with_object;
  }
  if (obj->cpuset && hwloc_obj_type_is_special(obj->type)) {
    if (hwloc__xml_verbose())
      fprintf(stderr, "invalid special object %s with cpuset\n",
	      hwloc_type_name(obj->type));
    goto error_with_object;
  }

  /* check parent vs child sets */
  if (obj->cpuset && parent && !parent->cpuset) {
    if (hwloc__xml_verbose())
      fprintf(stderr, "invalid object %s P#%u with cpuset while parent has none\n",
	      hwloc_type_name(obj->type), obj->os_index);
    goto error_with_object;
  }
  if (obj->nodeset && parent && !parent->nodeset) {
    if (hwloc__xml_verbose())
      fprintf(stderr, "invalid object %s P#%u with nodeset while parent has none\n",
	      hwloc_type_name(obj->type), obj->os_index);
    goto error_with_object;
  }

  /* check set consistency.
   * 1.7.2 and earlier reported I/O Groups with only a cpuset, we don't want to reject those XMLs yet.
   * Ignore those Groups since fixing the missing sets is hard (would need to look at children sets which are not available yet).
   * Just abort the XML for non-Groups.
   */
  if (!obj->cpuset != !obj->allowed_cpuset
      || !obj->cpuset != !obj->complete_cpuset) {
    /* has some cpuset without others */
    if (obj->type == HWLOC_OBJ_GROUP)
      ignored = 1;
    else
      goto error_with_object;
  } else if (!obj->nodeset != !obj->allowed_nodeset
	     || !obj->nodeset != !obj->complete_nodeset) {
    /* has some nodeset withot others */
    if (obj->type == HWLOC_OBJ_GROUP)
      ignored = 1;
    else
      goto error_with_object;
  } else if (obj->nodeset && !obj->cpuset) {
    /* has nodesets without cpusets (the contrary is allowed in pre-2.0) */
    if (obj->type == HWLOC_OBJ_GROUP)
      ignored = 1;
    else
      goto error_with_object;
  }

  /* check NUMA nodes */
  if (obj->type == HWLOC_OBJ_NUMANODE) {
    if (!obj->nodeset) {
      if (hwloc__xml_verbose())
	fprintf(stderr, "invalid NUMA node object P#%u without nodeset\n",
		obj->os_index);
      goto error_with_object;
    }
    data->nbnumanodes++;
    obj->prev_cousin = data->last_numanode;
    obj->next_cousin = NULL;
    if (data->last_numanode)
      data->last_numanode->next_cousin = obj;
    else
      data->first_numanode = obj;
    data->last_numanode = obj;
  }

  if (!hwloc_filter_check_keep_object(topology, obj)) {
    /* Ignore this object instead of inserting it.
     *
     * Well, let the core ignore the root object later
     * because we don't know yet if root has more than one child.
     */
    if (parent)
      ignored = 1;
  }

  if (parent && !ignored) {
    /* root->parent is NULL, and root is already inserted */
    hwloc_insert_object_by_parent(topology, parent, obj);
    /* insert_object_by_parent() doesn't merge during insert, so obj is still valid */
  }

  /* process subnodes */
  while (1) {
    struct hwloc__xml_import_state_s childstate;
    char *tag;
    int ret;

    ret = state->global->find_child(state, &childstate, &tag);
    if (ret < 0)
      goto error;
    if (!ret)
      break;

    if (!strcmp(tag, "object")) {
      hwloc_obj_t childobj = hwloc_alloc_setup_object(topology, HWLOC_OBJ_TYPE_MAX, -1);
      ret = hwloc__xml_import_object(topology, data, ignored ? parent : obj, childobj,
				     &childrengotignored,
				     &childstate);
    } else if (!strcmp(tag, "page_type")) {
      ret = hwloc__xml_import_pagetype(topology, obj, &childstate);
    } else if (!strcmp(tag, "info")) {
      ret = hwloc__xml_import_info(topology, obj, &childstate);
    } else if (!strcmp(tag, "distances")) {
      ret = hwloc__xml_import_v1distances(data, obj, &childstate);
    } else if (!strcmp(tag, "userdata")) {
      ret = hwloc__xml_import_userdata(topology, obj, &childstate);
    } else
      ret = -1;

    if (ret < 0)
      goto error;

    state->global->close_child(&childstate);
  }

  if (ignored) {
    /* drop that object, and tell the parent that one child got ignored */
    hwloc_free_unlinked_object(obj);
    *gotignored = 1;

  } else if (obj->first_child) {
    /* now that all children are inserted, make sure they are in-order,
     * so that the core doesn't have to deal with crappy children list.
     */
    hwloc_obj_t cur, next;
    for(cur = obj->first_child, next = cur->next_sibling;
	next;
	cur = next, next = next->next_sibling) {
      /* If reordering is needed, at least one pair of consecutive children will be out-of-order.
       * So just check pairs of consecutive children.
       *
       * We checked above that complete_cpuset is always set.
       */
      if (hwloc_bitmap_compare_first(next->complete_cpuset, cur->complete_cpuset) < 0) {
	/* next should be before cur */
	if (!childrengotignored) {
	  static int reported = 0;
	  if (!reported && !hwloc_hide_errors()) {
	    hwloc__xml_import_report_outoforder(topology, next, cur);
	    reported = 1;
	  }
	}
	hwloc__reorder_children(obj);
	break;
      }
    }
  }

  return state->global->close_tag(state);

 error_with_object:
  if (parent)
    /* root->parent is NULL, and root is already inserted. the caller will cleanup that root. */
    hwloc_free_unlinked_object(obj);
 error:
  return -1;
}

static int
hwloc__xml_import_v2distances(hwloc_topology_t topology,
			      hwloc__xml_import_state_t state)
{
  hwloc_obj_type_t type = HWLOC_OBJ_TYPE_NONE;
  unsigned nbobjs = 0;
  int indexing = 0;
  int os_indexing = 0;
  int gp_indexing = 0;
  unsigned long kind = 0;
  unsigned nr_indexes, nr_u64values;
  uint64_t *indexes;
  uint64_t *u64values;
  int ret;

  /* process attributes */
  while (1) {
    char *attrname, *attrvalue;
    if (state->global->next_attr(state, &attrname, &attrvalue) < 0)
      break;
    if (!strcmp(attrname, "nbobjs"))
      nbobjs = strtoul(attrvalue, NULL, 10);
    else if (!strcmp(attrname, "type")) {
      if (hwloc_type_sscanf(attrvalue, &type, NULL, 0) < 0)
	goto out;
    }
    else if (!strcmp(attrname, "indexing")) {
      indexing = 1;
      if (!strcmp(attrvalue, "os"))
	os_indexing = 1;
      else if (!strcmp(attrvalue, "gp"))
	gp_indexing = 1;
    }
    else if (!strcmp(attrname, "kind")) {
      kind = strtoul(attrvalue, NULL, 10);
    }
    else {
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: ignoring unknown distance attribute %s\n",
		state->global->msgprefix, attrname);
    }
  }

  /* abort if missing attribute */
  if (!nbobjs || type == HWLOC_OBJ_TYPE_NONE || !indexing || !kind) {
    if (hwloc__xml_verbose())
      fprintf(stderr, "%s: distance2 missing some attributes\n",
	      state->global->msgprefix);
    goto out;
  }

  indexes = malloc(nbobjs*sizeof(*indexes));
  u64values = malloc(nbobjs*nbobjs*sizeof(*u64values));
  if (!indexes || !u64values) {
    if (hwloc__xml_verbose())
      fprintf(stderr, "%s: failed to allocate distances arrays for %u objects\n",
	      state->global->msgprefix, nbobjs);
    goto out_with_arrays;
  }

  /* process children */
  nr_indexes = 0;
  nr_u64values = 0;
  while (1) {
    struct hwloc__xml_import_state_s childstate;
    char *attrname, *attrvalue, *tag, *buffer;
    int length;
    int is_index = 0;
    int is_u64values = 0;

    ret = state->global->find_child(state, &childstate, &tag);
    if (ret <= 0)
      break;

    if (!strcmp(tag, "indexes"))
      is_index = 1;
    else if (!strcmp(tag, "u64values"))
      is_u64values = 1;
    if (!is_index && !is_u64values) {
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: distance2 with unrecognized child %s\n",
		state->global->msgprefix, tag);
      goto out_with_arrays;
    }

    if (state->global->next_attr(&childstate, &attrname, &attrvalue) < 0
	|| strcmp(attrname, "length")) {
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: distance2 child must have length attribute\n",
		state->global->msgprefix);
      goto out_with_arrays;
    }
    length = atoi(attrvalue);

    ret = state->global->get_content(&childstate, &buffer, length);
    if (ret < 0) {
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: distance2 child needs content of length %d\n",
		state->global->msgprefix, length);
      goto out_with_arrays;
    }

    if (is_index) {
      /* get indexes */
      char *tmp;
      if (nr_indexes >= nbobjs) {
	if (hwloc__xml_verbose())
	  fprintf(stderr, "%s: distance2 with more than %d indexes\n",
		  state->global->msgprefix, nbobjs);
	goto out_with_arrays;
      }
      tmp = buffer;
      while (1) {
	char *next;
	unsigned long long u = strtoull(tmp, &next, 0);
	if (next == tmp)
	  break;
	indexes[nr_indexes++] = u;
	if (*next != ' ')
	  break;
	if (nr_indexes == nbobjs)
	  break;
	tmp = next+1;
      }

    } else if (is_u64values) {
      /* get uint64_t values */
      char *tmp;
      if (nr_u64values >= nbobjs*nbobjs) {
	if (hwloc__xml_verbose())
	  fprintf(stderr, "%s: distance2 with more than %d u64values\n",
		  state->global->msgprefix, nbobjs*nbobjs);
	goto out_with_arrays;
      }
      tmp = buffer;
      while (1) {
	char *next;
	unsigned long long u = strtoull(tmp, &next, 0);
	if (next == tmp)
	  break;
	u64values[nr_u64values++] = u;
	if (*next != ' ')
	  break;
	if (nr_u64values == nbobjs*nbobjs)
	  break;
	tmp = next+1;
      }
    }

    state->global->close_content(&childstate);

    ret = state->global->close_tag(&childstate);
    if (ret < 0) {
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: distance2 with more than %d indexes\n",
		state->global->msgprefix, nbobjs);
      goto out_with_arrays;
    }

    state->global->close_child(&childstate);
  }

  if (nr_indexes != nbobjs) {
    if (hwloc__xml_verbose())
      fprintf(stderr, "%s: distance2 with less than %d indexes\n",
	      state->global->msgprefix, nbobjs);
    goto out_with_arrays;
  }
  if (nr_u64values != nbobjs*nbobjs) {
    if (hwloc__xml_verbose())
      fprintf(stderr, "%s: distance2 with less than %d u64values\n",
	      state->global->msgprefix, nbobjs*nbobjs);
    goto out_with_arrays;
  }

  if (nbobjs < 2) {
    /* distances with a single object are useless, even if the XML isn't invalid */
    if (hwloc__xml_verbose())
      fprintf(stderr, "%s: ignoring distances2 with only %d object\n",
	      state->global->msgprefix, nbobjs);
    goto out_ignore;
  }
  if (type == HWLOC_OBJ_PU || type == HWLOC_OBJ_NUMANODE) {
    if (!os_indexing) {
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: ignoring PU or NUMA distances2 without os_indexing\n",
		state->global->msgprefix);
      goto out_ignore;
    }
  } else {
    if (!gp_indexing) {
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: ignoring !PU or !NUMA distances2 without gp_indexing\n",
		state->global->msgprefix);
      goto out_ignore;
    }
  }

  hwloc_internal_distances_add_by_index(topology, type, nbobjs, indexes, u64values, kind, 0);

  /* prevent freeing below */
  indexes = NULL;
  u64values = NULL;

 out_ignore:
  free(indexes);
  free(u64values);
  return state->global->close_tag(state);

 out_with_arrays:
  free(indexes);
  free(u64values);
 out:
  return -1;
}

static int
hwloc__xml_import_diff_one(hwloc__xml_import_state_t state,
			   hwloc_topology_diff_t *firstdiffp,
			   hwloc_topology_diff_t *lastdiffp)
{
  char *type_s = NULL;
  char *obj_depth_s = NULL;
  char *obj_index_s = NULL;
  char *obj_attr_type_s = NULL;
/* char *obj_attr_index_s = NULL; unused for now */
  char *obj_attr_name_s = NULL;
  char *obj_attr_oldvalue_s = NULL;
  char *obj_attr_newvalue_s = NULL;

  while (1) {
    char *attrname, *attrvalue;
    if (state->global->next_attr(state, &attrname, &attrvalue) < 0)
      break;
    if (!strcmp(attrname, "type"))
      type_s = attrvalue;
    else if (!strcmp(attrname, "obj_depth"))
      obj_depth_s = attrvalue;
    else if (!strcmp(attrname, "obj_index"))
      obj_index_s = attrvalue;
    else if (!strcmp(attrname, "obj_attr_type"))
      obj_attr_type_s = attrvalue;
    else if (!strcmp(attrname, "obj_attr_index"))
      { /* obj_attr_index_s = attrvalue; unused for now */ }
    else if (!strcmp(attrname, "obj_attr_name"))
      obj_attr_name_s = attrvalue;
    else if (!strcmp(attrname, "obj_attr_oldvalue"))
      obj_attr_oldvalue_s = attrvalue;
    else if (!strcmp(attrname, "obj_attr_newvalue"))
      obj_attr_newvalue_s = attrvalue;
    else {
      if (hwloc__xml_verbose())
	fprintf(stderr, "%s: ignoring unknown diff attribute %s\n",
		state->global->msgprefix, attrname);
      return -1;
    }
  }

  if (type_s) {
    switch (atoi(type_s)) {
    default:
      break;
    case HWLOC_TOPOLOGY_DIFF_OBJ_ATTR: {
      /* object attribute diff */
      hwloc_topology_diff_obj_attr_type_t obj_attr_type;
      hwloc_topology_diff_t diff;

      /* obj_attr mandatory generic attributes */
      if (!obj_depth_s || !obj_index_s || !obj_attr_type_s) {
	if (hwloc__xml_verbose())
	  fprintf(stderr, "%s: missing mandatory obj attr generic attributes\n",
		  state->global->msgprefix);
	break;
      }

      /* obj_attr mandatory attributes common to all subtypes */
      if (!obj_attr_oldvalue_s || !obj_attr_newvalue_s) {
	if (hwloc__xml_verbose())
	  fprintf(stderr, "%s: missing mandatory obj attr value attributes\n",
		  state->global->msgprefix);
	break;
      }

      /* mandatory attributes for obj_attr_info subtype */
      obj_attr_type = atoi(obj_attr_type_s);
      if (obj_attr_type == HWLOC_TOPOLOGY_DIFF_OBJ_ATTR_INFO && !obj_attr_name_s) {
	if (hwloc__xml_verbose())
	  fprintf(stderr, "%s: missing mandatory obj attr info name attribute\n",
		  state->global->msgprefix);
	break;
      }

      /* now we know we have everything we need */
      diff = malloc(sizeof(*diff));
      if (!diff)
	return -1;
      diff->obj_attr.type = HWLOC_TOPOLOGY_DIFF_OBJ_ATTR;
      diff->obj_attr.obj_depth = atoi(obj_depth_s);
      diff->obj_attr.obj_index = atoi(obj_index_s);
      memset(&diff->obj_attr.diff, 0, sizeof(diff->obj_attr.diff));
      diff->obj_attr.diff.generic.type = obj_attr_type;

      switch (atoi(obj_attr_type_s)) {
      case HWLOC_TOPOLOGY_DIFF_OBJ_ATTR_SIZE:
	diff->obj_attr.diff.uint64.oldvalue = strtoull(obj_attr_oldvalue_s, NULL, 0);
	diff->obj_attr.diff.uint64.newvalue = strtoull(obj_attr_newvalue_s, NULL, 0);
	break;
      case HWLOC_TOPOLOGY_DIFF_OBJ_ATTR_INFO:
	diff->obj_attr.diff.string.name = strdup(obj_attr_name_s);
	/* fallthrough */
      case HWLOC_TOPOLOGY_DIFF_OBJ_ATTR_NAME:
	diff->obj_attr.diff.string.oldvalue = strdup(obj_attr_oldvalue_s);
	diff->obj_attr.diff.string.newvalue = strdup(obj_attr_newvalue_s);
	break;
      }

      if (*firstdiffp)
	(*lastdiffp)->generic.next = diff;
      else
        *firstdiffp = diff;
      *lastdiffp = diff;
      diff->generic.next = NULL;
    }
    }
  }

  return state->global->close_tag(state);
}

int
hwloc__xml_import_diff(hwloc__xml_import_state_t state,
		       hwloc_topology_diff_t *firstdiffp)
{
  hwloc_topology_diff_t firstdiff = NULL, lastdiff = NULL;
  *firstdiffp = NULL;

  while (1) {
    struct hwloc__xml_import_state_s childstate;
    char *tag;
    int ret;

    ret = state->global->find_child(state, &childstate, &tag);
    if (ret < 0)
      return -1;
    if (!ret)
      break;

    if (!strcmp(tag, "diff")) {
      ret = hwloc__xml_import_diff_one(&childstate, &firstdiff, &lastdiff);
    } else
      ret = -1;

    if (ret < 0)
      return ret;

    state->global->close_child(&childstate);
  }

  *firstdiffp = firstdiff;
  return 0;
}

/***********************************
 ********* main XML import *********
 ***********************************/

static void
hwloc_convert_from_v1dist_floats(hwloc_topology_t topology, unsigned nbobjs, float *floats, uint64_t *u64s)
{
  unsigned i;
  int is_uint;
  char *env;
  float scale = 1000.f;
  char scalestring[20];

  env = getenv("HWLOC_XML_V1DIST_SCALE");
  if (env) {
    scale = atof(env);
    goto scale;
  }

  is_uint = 1;
  /* find out if all values are integers */
  for(i=0; i<nbobjs*nbobjs; i++) {
    float f, iptr, fptr;
    f = floats[i];
    if (f < 0.f) {
      is_uint = 0;
      break;
    }
    fptr = modff(f, &iptr);
    if (fptr > .001f && fptr < .999f) {
      is_uint = 0;
      break;
    }
    u64s[i] = (int)(f+.5f);
  }
  if (is_uint)
    return;

 scale:
  /* TODO heuristic to find a good scale */
  for(i=0; i<nbobjs*nbobjs; i++)
    u64s[i] = scale * floats[i];

  /* save the scale in root info attrs.
   * Not perfect since we may have multiple of them,
   * and some distances might disappear in case f restrict, etc.
   */
  sprintf(scalestring, "%f", scale);
  hwloc_obj_add_info(hwloc_get_root_obj(topology), "xmlv1DistancesScale", scalestring);
}

/* this canNOT be the first XML call */
static int
hwloc_look_xml(struct hwloc_backend *backend)
{
  struct hwloc_topology *topology = backend->topology;
  struct hwloc_xml_backend_data_s *data = backend->private_data;
  struct hwloc__xml_import_state_s state, childstate;
  struct hwloc_obj *root = topology->levels[0][0];
  char *tag;
  int gotignored = 0;
  hwloc_localeswitch_declare;
  int ret;

  state.global = data;

  assert(!root->cpuset);

  hwloc_localeswitch_init();

  data->nbnumanodes = 0;
  data->first_numanode = data->last_numanode = NULL;
  data->first_v1dist = data->last_v1dist = NULL;

  ret = data->look_init(data, &state);
  if (ret < 0)
    goto failed;

  /* find root object tag and import it */
  ret = state.global->find_child(&state, &childstate, &tag);
  if (ret < 0 || !ret || strcmp(tag, "object"))
    goto failed;
  ret = hwloc__xml_import_object(topology, data, NULL /*  no parent */, root,
				 &gotignored,
				 &childstate);
  if (ret < 0)
    goto failed;
  state.global->close_child(&childstate);
  assert(!gotignored);

  /* find v2 distances */
  while (1) {
    ret = state.global->find_child(&state, &childstate, &tag);
    if (ret < 0)
      goto failed;
    if (!ret)
      break;
    if (strcmp(tag, "distances2"))
      goto failed;
    ret = hwloc__xml_import_v2distances(topology, &childstate);
    if (ret < 0)
      goto failed;
    state.global->close_child(&childstate);
  }

  /* find end of topology tag */
  state.global->close_tag(&state);

  if (!root->cpuset) {
    if (hwloc__xml_verbose())
      fprintf(stderr, "invalid root object without cpuset\n");
    goto err;
  }

  /* handle v1 distances */
  if (data->first_v1dist) {
    struct hwloc__xml_imported_v1distances_s *v1dist, *v1next = data->first_v1dist;
    while ((v1dist = v1next) != NULL) {
      unsigned nbobjs = v1dist->nbobjs;
      v1next = v1dist->next;
      /* Handle distances as NUMA node distances if nbobjs matches.
       * Otherwise drop, only NUMA distances really matter.
       *
       * We could also attach to a random level with the right nbobjs,
       * but it would require to have those objects in the original XML order (like the first_numanode cousin-list).
       * because the topology order can be different if some parents are ignored during load.
       */
      if (nbobjs == data->nbnumanodes) {
	hwloc_obj_t *objs = malloc(nbobjs*sizeof(hwloc_obj_t));
	uint64_t *values = malloc(nbobjs*nbobjs*sizeof(*values));
	if (objs && values) {
	  hwloc_obj_t node;
	  unsigned i;
	  for(i=0, node = data->first_numanode;
	      i<nbobjs;
	      i++, node = node->next_cousin)
	    objs[i] = node;
hwloc_convert_from_v1dist_floats(topology, nbobjs, v1dist->floats, values);
	  hwloc_internal_distances_add(topology, nbobjs, objs, values, v1dist->kind, 0);
	} else {
	  free(objs);
	  free(values);
	}
      }
      free(v1dist->floats);
      free(v1dist);
    }
    data->first_v1dist = data->last_v1dist = NULL;
  }

  /* FIXME:
   * We should check that the existing object sets are consistent:
   * no intersection between objects of a same level,
   * object sets included in parent sets.
   * hwloc never generated such buggy XML, but users could create one.
   *
   * We want to add these checks to the existing core code that
   * adds missing sets and propagates parent/children sets
   * (in case another backend ever generates buggy object sets as well).
   */

  if (!data->nbnumanodes) {
    /* before 2.0, XML could have no NUMA node objects and no nodesets */
    hwloc_obj_t numa;
    /* create missing root nodesets and make sure they are consistent with the upcoming NUMA node */
    if (!root->nodeset)
      root->nodeset = hwloc_bitmap_alloc();
    if (!root->allowed_nodeset)
      root->allowed_nodeset = hwloc_bitmap_alloc();
    if (!root->complete_nodeset)
      root->complete_nodeset = hwloc_bitmap_alloc();
    hwloc_bitmap_only(root->nodeset, 0);
    hwloc_bitmap_only(root->allowed_nodeset, 0);
    hwloc_bitmap_only(root->complete_nodeset, 0);
    /* add a NUMA node and move the root memory there */
    numa = hwloc_alloc_setup_object(topology, HWLOC_OBJ_NUMANODE, 0);
    numa->cpuset = hwloc_bitmap_dup(root->cpuset);
    numa->nodeset = hwloc_bitmap_alloc();
    hwloc_bitmap_set(numa->nodeset, 0);
    memcpy(&numa->memory, &topology->levels[0][0]->memory, sizeof(numa->memory));
    memset(&topology->levels[0][0]->memory, 0, sizeof(numa->memory));
    /* insert by cpuset so that it goes between root and its existing children */
    hwloc_insert_object_by_cpuset(topology, numa);
  }

  /* make sure we have a nodeset now. if we got NUMA nodes without nodeset, something bad happened */
  if (!root->nodeset) {
    if (hwloc__xml_verbose())
      fprintf(stderr, "invalid root object without nodeset\n");
    goto err;
  }

  /* allocate default cpusets and nodesets if missing, the core will restrict them */
  hwloc_alloc_obj_cpusets(root);

  /* keep the "Backend" information intact */
  /* we could add "BackendSource=XML" to notify that XML was used between the actual backend and here */

  topology->support.discovery->pu = 1;

  hwloc_localeswitch_fini();
  return 0;

 failed:
  if (data->look_failed)
    data->look_failed(data);
  if (hwloc__xml_verbose())
    fprintf(stderr, "%s: XML component discovery failed.\n",
	    data->msgprefix);
 err:
  hwloc_free_object_siblings_and_children(root->first_child);
  root->first_child = NULL;
  hwloc_free_object_siblings_and_children(root->io_first_child);
  root->io_first_child = NULL;
  hwloc_free_object_siblings_and_children(root->misc_first_child);
  root->misc_first_child = NULL;

  hwloc_localeswitch_fini();
  return -1;
}

/* this can be the first XML call */
int
hwloc_topology_diff_load_xml(const char *xmlpath,
			     hwloc_topology_diff_t *firstdiffp, char **refnamep)
{
  struct hwloc__xml_import_state_s state;
  struct hwloc_xml_backend_data_s fakedata; /* only for storing global info during parsing */
  hwloc_localeswitch_declare;
  const char *basename;
  int force_nolibxml;
  int ret;

  state.global = &fakedata;

  basename = strrchr(xmlpath, '/');
  if (basename)
    basename++;
  else
    basename = xmlpath;
  fakedata.msgprefix = strdup(basename);

  hwloc_components_init();
  assert(hwloc_nolibxml_callbacks);

  hwloc_localeswitch_init();

  *firstdiffp = NULL;

  force_nolibxml = hwloc_nolibxml_import();
retry:
  if (!hwloc_libxml_callbacks || (hwloc_nolibxml_callbacks && force_nolibxml))
    ret = hwloc_nolibxml_callbacks->import_diff(&state, xmlpath, NULL, 0, firstdiffp, refnamep);
  else {
    ret = hwloc_libxml_callbacks->import_diff(&state, xmlpath, NULL, 0, firstdiffp, refnamep);
    if (ret < 0 && errno == ENOSYS) {
      hwloc_libxml_callbacks = NULL;
      goto retry;
    }
  }

  hwloc_localeswitch_fini();
  hwloc_components_fini();
  free(fakedata.msgprefix);
  return ret;
}

/* this can be the first XML call */
int
hwloc_topology_diff_load_xmlbuffer(const char *xmlbuffer, int buflen,
				   hwloc_topology_diff_t *firstdiffp, char **refnamep)
{
  struct hwloc__xml_import_state_s state;
  struct hwloc_xml_backend_data_s fakedata; /* only for storing global info during parsing */
  hwloc_localeswitch_declare;
  int force_nolibxml;
  int ret;

  state.global = &fakedata;
  fakedata.msgprefix = strdup("xmldiffbuffer");

  hwloc_components_init();
  assert(hwloc_nolibxml_callbacks);

  hwloc_localeswitch_init();

  *firstdiffp = NULL;

  force_nolibxml = hwloc_nolibxml_import();
 retry:
  if (!hwloc_libxml_callbacks || (hwloc_nolibxml_callbacks && force_nolibxml))
    ret = hwloc_nolibxml_callbacks->import_diff(&state, NULL, xmlbuffer, buflen, firstdiffp, refnamep);
  else {
    ret = hwloc_libxml_callbacks->import_diff(&state, NULL, xmlbuffer, buflen, firstdiffp, refnamep);
    if (ret < 0 && errno == ENOSYS) {
      hwloc_libxml_callbacks = NULL;
      goto retry;
    }
  }

  hwloc_localeswitch_fini();
  hwloc_components_fini();
  free(fakedata.msgprefix);
  return ret;
}

/************************************************
 ********* XML export (common routines) *********
 ************************************************/

#define HWLOC_XML_CHAR_VALID(c) (((c) >= 32 && (c) <= 126) || (c) == '\t' || (c) == '\n' || (c) == '\r')

static int
hwloc__xml_export_check_buffer(const char *buf, size_t length)
{
  unsigned i;
  for(i=0; i<length; i++)
    if (!HWLOC_XML_CHAR_VALID(buf[i]))
      return -1;
  return 0;
}

/* strdup and remove ugly chars from random string */
static char*
hwloc__xml_export_safestrdup(const char *old)
{
  char *new = malloc(strlen(old)+1);
  char *dst = new;
  const char *src = old;
  while (*src) {
    if (HWLOC_XML_CHAR_VALID(*src))
      *(dst++) = *src;
    src++;
  }
  *dst = '\0';
  return new;
}

static void
hwloc__xml_export_object (hwloc__xml_export_state_t parentstate, hwloc_topology_t topology, hwloc_obj_t obj)
{
  struct hwloc__xml_export_state_s state;
  hwloc_obj_t child;
  char *cpuset = NULL;
  char tmp[255];
  unsigned i;

  parentstate->new_child(parentstate, &state, "object");

  state.new_prop(&state, "type", hwloc_type_name(obj->type));
  if (obj->os_index != (unsigned) -1) {
    sprintf(tmp, "%u", obj->os_index);
    state.new_prop(&state, "os_index", tmp);
  }
  if (obj->cpuset) {
    hwloc_bitmap_asprintf(&cpuset, obj->cpuset);
    state.new_prop(&state, "cpuset", cpuset);
    free(cpuset);
  }
  if (obj->complete_cpuset) {
    hwloc_bitmap_asprintf(&cpuset, obj->complete_cpuset);
    state.new_prop(&state, "complete_cpuset", cpuset);
    free(cpuset);
  }
  if (obj->allowed_cpuset) {
    hwloc_bitmap_asprintf(&cpuset, obj->allowed_cpuset);
    state.new_prop(&state, "allowed_cpuset", cpuset);
    free(cpuset);
  }
  if (obj->nodeset && !hwloc_bitmap_isfull(obj->nodeset)) {
    hwloc_bitmap_asprintf(&cpuset, obj->nodeset);
    state.new_prop(&state, "nodeset", cpuset);
    free(cpuset);
  }
  if (obj->complete_nodeset && !hwloc_bitmap_isfull(obj->complete_nodeset)) {
    hwloc_bitmap_asprintf(&cpuset, obj->complete_nodeset);
    state.new_prop(&state, "complete_nodeset", cpuset);
    free(cpuset);
  }
  if (obj->allowed_nodeset && !hwloc_bitmap_isfull(obj->allowed_nodeset)) {
    hwloc_bitmap_asprintf(&cpuset, obj->allowed_nodeset);
    state.new_prop(&state, "allowed_nodeset", cpuset);
    free(cpuset);
  }

  sprintf(tmp, "%llu", (unsigned long long) obj->gp_index);
  state.new_prop(&state, "gp_index", tmp);

  if (obj->name) {
    char *name = hwloc__xml_export_safestrdup(obj->name);
    state.new_prop(&state, "name", name);
    free(name);
  }
  if (obj->subtype) {
    char *subtype = hwloc__xml_export_safestrdup(obj->subtype);
    state.new_prop(&state, "subtype", subtype);
    free(subtype);
  }

  switch (obj->type) {
  case HWLOC_OBJ_L1CACHE:
  case HWLOC_OBJ_L2CACHE:
  case HWLOC_OBJ_L3CACHE:
  case HWLOC_OBJ_L4CACHE:
  case HWLOC_OBJ_L5CACHE:
  case HWLOC_OBJ_L1ICACHE:
  case HWLOC_OBJ_L2ICACHE:
  case HWLOC_OBJ_L3ICACHE:
    sprintf(tmp, "%llu", (unsigned long long) obj->attr->cache.size);
    state.new_prop(&state, "cache_size", tmp);
    sprintf(tmp, "%u", obj->attr->cache.depth);
    state.new_prop(&state, "depth", tmp);
    sprintf(tmp, "%u", (unsigned) obj->attr->cache.linesize);
    state.new_prop(&state, "cache_linesize", tmp);
    sprintf(tmp, "%d", (unsigned) obj->attr->cache.associativity);
    state.new_prop(&state, "cache_associativity", tmp);
    sprintf(tmp, "%d", (unsigned) obj->attr->cache.type);
    state.new_prop(&state, "cache_type", tmp);
    break;
  case HWLOC_OBJ_GROUP:
    sprintf(tmp, "%u", obj->attr->group.depth);
    state.new_prop(&state, "depth", tmp);
    sprintf(tmp, "%u", obj->attr->group.kind);
    state.new_prop(&state, "kind", tmp);
    sprintf(tmp, "%u", obj->attr->group.subkind);
    state.new_prop(&state, "subkind", tmp);
    break;
  case HWLOC_OBJ_BRIDGE:
    sprintf(tmp, "%u-%u", obj->attr->bridge.upstream_type, obj->attr->bridge.downstream_type);
    state.new_prop(&state, "bridge_type", tmp);
    sprintf(tmp, "%u", obj->attr->bridge.depth);
    state.new_prop(&state, "depth", tmp);
    if (obj->attr->bridge.downstream_type == HWLOC_OBJ_BRIDGE_PCI) {
      sprintf(tmp, "%04x:[%02x-%02x]",
	      (unsigned) obj->attr->bridge.downstream.pci.domain,
	      (unsigned) obj->attr->bridge.downstream.pci.secondary_bus,
	      (unsigned) obj->attr->bridge.downstream.pci.subordinate_bus);
      state.new_prop(&state, "bridge_pci", tmp);
    }
    if (obj->attr->bridge.upstream_type != HWLOC_OBJ_BRIDGE_PCI)
      break;
    /* fallthrough */
  case HWLOC_OBJ_PCI_DEVICE:
    sprintf(tmp, "%04x:%02x:%02x.%01x",
	    (unsigned) obj->attr->pcidev.domain,
	    (unsigned) obj->attr->pcidev.bus,
	    (unsigned) obj->attr->pcidev.dev,
	    (unsigned) obj->attr->pcidev.func);
    state.new_prop(&state, "pci_busid", tmp);
    sprintf(tmp, "%04x [%04x:%04x] [%04x:%04x] %02x",
	    (unsigned) obj->attr->pcidev.class_id,
	    (unsigned) obj->attr->pcidev.vendor_id, (unsigned) obj->attr->pcidev.device_id,
	    (unsigned) obj->attr->pcidev.subvendor_id, (unsigned) obj->attr->pcidev.subdevice_id,
	    (unsigned) obj->attr->pcidev.revision);
    state.new_prop(&state, "pci_type", tmp);
    sprintf(tmp, "%f", obj->attr->pcidev.linkspeed);
    state.new_prop(&state, "pci_link_speed", tmp);
    break;
  case HWLOC_OBJ_OS_DEVICE:
    sprintf(tmp, "%u", obj->attr->osdev.type);
    state.new_prop(&state, "osdev_type", tmp);
    break;
  default:
    break;
  }

  if (obj->memory.local_memory) {
    sprintf(tmp, "%llu", (unsigned long long) obj->memory.local_memory);
    state.new_prop(&state, "local_memory", tmp);
  }

  for(i=0; i<obj->memory.page_types_len; i++) {
    struct hwloc__xml_export_state_s childstate;
    state.new_child(&state, &childstate, "page_type");
    sprintf(tmp, "%llu", (unsigned long long) obj->memory.page_types[i].size);
    childstate.new_prop(&childstate, "size", tmp);
    sprintf(tmp, "%llu", (unsigned long long) obj->memory.page_types[i].count);
    childstate.new_prop(&childstate, "count", tmp);
    childstate.end_object(&childstate, "page_type");
  }

  for(i=0; i<obj->infos_count; i++) {
    char *name = hwloc__xml_export_safestrdup(obj->infos[i].name);
    char *value = hwloc__xml_export_safestrdup(obj->infos[i].value);
    struct hwloc__xml_export_state_s childstate;
    state.new_child(&state, &childstate, "info");
    childstate.new_prop(&childstate, "name", name);
    childstate.new_prop(&childstate, "value", value);
    childstate.end_object(&childstate, "info");
    free(name);
    free(value);
  }

#if 0
  // FIXME, if v1 and root, export distances that cover all children
  for(i=0; i<obj->distances_count; i++) {
    unsigned nbobjs = obj->distances[i]->nbobjs;
    unsigned j;
    struct hwloc__xml_export_state_s childstate;
    state.new_child(&state, &childstate, "distances");
    sprintf(tmp, "%u", nbobjs);
    childstate.new_prop(&childstate, "nbobjs", tmp);
    sprintf(tmp, "%u", obj->distances[i]->relative_depth);
    childstate.new_prop(&childstate, "relative_depth", tmp);
    sprintf(tmp, "%f", obj->distances[i]->latency_base);
    childstate.new_prop(&childstate, "latency_base", tmp);
    for(j=0; j<nbobjs*nbobjs; j++) {
      struct hwloc__xml_export_state_s greatchildstate;
      childstate.new_child(&childstate, &greatchildstate, "latency");
      sprintf(tmp, "%f", obj->distances[i]->latency[j]);
      greatchildstate.new_prop(&greatchildstate, "value", tmp);
      greatchildstate.end_object(&greatchildstate, "latency");
    }
    childstate.end_object(&childstate, "distances");
  }
#endif

  if (obj->userdata && topology->userdata_export_cb)
    topology->userdata_export_cb((void*) &state, topology, obj);

  for(child = obj->first_child; child; child = child->next_sibling)
    hwloc__xml_export_object (&state, topology, child);
  for(child = obj->io_first_child; child; child = child->next_sibling)
    hwloc__xml_export_object (&state, topology, child);
  for(child = obj->misc_first_child; child; child = child->next_sibling)
    hwloc__xml_export_object (&state, topology, child);

  state.end_object(&state, "object");
}

#define EXPORT_ARRAY(state, type, nr, values, tagname, format, maxperline) do { \
  unsigned i = 0; \
  while (i<(nr)) { \
    char tmp[255]; /* enough for (snprintf(format)+space) x maxperline */ \
    char tmp2[16]; \
    size_t len = 0; \
    unsigned j; \
    struct hwloc__xml_export_state_s childstate; \
    (state)->new_child(state, &childstate, tagname); \
    for(j=0; \
	i+j<(nr) && j<maxperline; \
	j++) \
      len += sprintf(tmp+len, format " ", (type) (values)[i+j]); \
    i += j; \
    sprintf(tmp2, "%lu", (unsigned long) len); \
    childstate.new_prop(&childstate, "length", tmp2); \
    childstate.add_content(&childstate, tmp, len); \
    childstate.end_object(&childstate, tagname); \
  } \
} while (0)

static void
hwloc__xml_export_v2distances(hwloc__xml_export_state_t parentstate, hwloc_topology_t topology)
{
  struct hwloc_internal_distances_s *dist;
  for(dist = topology->first_dist; dist; dist = dist->next) {
    char tmp[255];
    unsigned nbobjs = dist->nbobjs;
    struct hwloc__xml_export_state_s state;

    parentstate->new_child(parentstate, &state, "distances2");

    state.new_prop(&state, "type", hwloc_type_name(dist->type));
    sprintf(tmp, "%u", nbobjs);
    state.new_prop(&state, "nbobjs", tmp);
    sprintf(tmp, "%lu", dist->kind);
    state.new_prop(&state, "kind", tmp);

    state.new_prop(&state, "indexing",
		   (dist->type == HWLOC_OBJ_NUMANODE || dist->type == HWLOC_OBJ_PU) ? "os" : "gp");
    /* TODO don't hardwire 10 below. either snprintf the max to guess it, or just append until the end of the buffer */
    EXPORT_ARRAY(&state, unsigned long long, nbobjs, dist->indexes, "indexes", "%llu", 10);
    EXPORT_ARRAY(&state, unsigned long long, nbobjs*nbobjs, dist->values, "u64values", "%llu", 10);
    state.end_object(&state, "distances2");
  }
}

void
hwloc__xml_export_topology(hwloc__xml_export_state_t state, hwloc_topology_t topology)
{
  hwloc__xml_export_object (state, topology, hwloc_get_root_obj(topology));
  hwloc__xml_export_v2distances (state, topology);
}

void
hwloc__xml_export_diff(hwloc__xml_export_state_t parentstate, hwloc_topology_diff_t diff)
{
  while (diff) {
    struct hwloc__xml_export_state_s state;
    char tmp[255];

    parentstate->new_child(parentstate, &state, "diff");

    sprintf(tmp, "%u", diff->generic.type);
    state.new_prop(&state, "type", tmp);

    switch (diff->generic.type) {
    case HWLOC_TOPOLOGY_DIFF_OBJ_ATTR:
      sprintf(tmp, "%d", diff->obj_attr.obj_depth);
      state.new_prop(&state, "obj_depth", tmp);
      sprintf(tmp, "%u", diff->obj_attr.obj_index);
      state.new_prop(&state, "obj_index", tmp);

      sprintf(tmp, "%u", diff->obj_attr.diff.generic.type);
      state.new_prop(&state, "obj_attr_type", tmp);

      switch (diff->obj_attr.diff.generic.type) {
      case HWLOC_TOPOLOGY_DIFF_OBJ_ATTR_SIZE:
	sprintf(tmp, "%llu", (unsigned long long) diff->obj_attr.diff.uint64.index);
	state.new_prop(&state, "obj_attr_index", tmp);
	sprintf(tmp, "%llu", (unsigned long long) diff->obj_attr.diff.uint64.oldvalue);
	state.new_prop(&state, "obj_attr_oldvalue", tmp);
	sprintf(tmp, "%llu", (unsigned long long) diff->obj_attr.diff.uint64.newvalue);
	state.new_prop(&state, "obj_attr_newvalue", tmp);
	break;
      case HWLOC_TOPOLOGY_DIFF_OBJ_ATTR_NAME:
      case HWLOC_TOPOLOGY_DIFF_OBJ_ATTR_INFO:
	if (diff->obj_attr.diff.string.name)
	  state.new_prop(&state, "obj_attr_name", diff->obj_attr.diff.string.name);
	state.new_prop(&state, "obj_attr_oldvalue", diff->obj_attr.diff.string.oldvalue);
	state.new_prop(&state, "obj_attr_newvalue", diff->obj_attr.diff.string.newvalue);
	break;
      }

      break;
    default:
      assert(0);
    }
    state.end_object(&state, "diff");

    diff = diff->generic.next;
  }
}

/**********************************
 ********* main XML export ********
 **********************************/

/* this can be the first XML call */
int hwloc_topology_export_xml(hwloc_topology_t topology, const char *filename)
{
  hwloc_localeswitch_declare;
  int force_nolibxml;
  int ret;

  assert(hwloc_nolibxml_callbacks); /* the core called components_init() for the topology */

  hwloc_localeswitch_init();

  force_nolibxml = hwloc_nolibxml_export();
retry:
  if (!hwloc_libxml_callbacks || (hwloc_nolibxml_callbacks && force_nolibxml))
    ret = hwloc_nolibxml_callbacks->export_file(topology, filename);
  else {
    ret = hwloc_libxml_callbacks->export_file(topology, filename);
    if (ret < 0 && errno == ENOSYS) {
      hwloc_libxml_callbacks = NULL;
      goto retry;
    }
  }

  hwloc_localeswitch_fini();
  return ret;
}

/* this can be the first XML call */
int hwloc_topology_export_xmlbuffer(hwloc_topology_t topology, char **xmlbuffer, int *buflen)
{
  hwloc_localeswitch_declare;
  int force_nolibxml;
  int ret;

  assert(hwloc_nolibxml_callbacks); /* the core called components_init() for the topology */

  hwloc_localeswitch_init();

  force_nolibxml = hwloc_nolibxml_export();
retry:
  if (!hwloc_libxml_callbacks || (hwloc_nolibxml_callbacks && force_nolibxml))
    ret = hwloc_nolibxml_callbacks->export_buffer(topology, xmlbuffer, buflen);
  else {
    ret = hwloc_libxml_callbacks->export_buffer(topology, xmlbuffer, buflen);
    if (ret < 0 && errno == ENOSYS) {
      hwloc_libxml_callbacks = NULL;
      goto retry;
    }
  }

  hwloc_localeswitch_fini();
  return ret;
}

/* this can be the first XML call */
int
hwloc_topology_diff_export_xml(hwloc_topology_diff_t diff, const char *refname,
			       const char *filename)
{
  hwloc_localeswitch_declare;
  hwloc_topology_diff_t tmpdiff;
  int force_nolibxml;
  int ret;

  tmpdiff = diff;
  while (tmpdiff) {
    if (tmpdiff->generic.type == HWLOC_TOPOLOGY_DIFF_TOO_COMPLEX) {
      errno = EINVAL;
      return -1;
    }
    tmpdiff = tmpdiff->generic.next;
  }

  hwloc_components_init();
  assert(hwloc_nolibxml_callbacks);

  hwloc_localeswitch_init();

  force_nolibxml = hwloc_nolibxml_export();
retry:
  if (!hwloc_libxml_callbacks || (hwloc_nolibxml_callbacks && force_nolibxml))
    ret = hwloc_nolibxml_callbacks->export_diff_file(diff, refname, filename);
  else {
    ret = hwloc_libxml_callbacks->export_diff_file(diff, refname, filename);
    if (ret < 0 && errno == ENOSYS) {
      hwloc_libxml_callbacks = NULL;
      goto retry;
    }
  }

  hwloc_localeswitch_fini();
  hwloc_components_fini();
  return ret;
}

/* this can be the first XML call */
int
hwloc_topology_diff_export_xmlbuffer(hwloc_topology_diff_t diff, const char *refname,
				     char **xmlbuffer, int *buflen)
{
  hwloc_localeswitch_declare;
  hwloc_topology_diff_t tmpdiff;
  int force_nolibxml;
  int ret;

  tmpdiff = diff;
  while (tmpdiff) {
    if (tmpdiff->generic.type == HWLOC_TOPOLOGY_DIFF_TOO_COMPLEX) {
      errno = EINVAL;
      return -1;
    }
    tmpdiff = tmpdiff->generic.next;
  }

  hwloc_components_init();
  assert(hwloc_nolibxml_callbacks);

  hwloc_localeswitch_init();

  force_nolibxml = hwloc_nolibxml_export();
retry:
  if (!hwloc_libxml_callbacks || (hwloc_nolibxml_callbacks && force_nolibxml))
    ret = hwloc_nolibxml_callbacks->export_diff_buffer(diff, refname, xmlbuffer, buflen);
  else {
    ret = hwloc_libxml_callbacks->export_diff_buffer(diff, refname, xmlbuffer, buflen);
    if (ret < 0 && errno == ENOSYS) {
      hwloc_libxml_callbacks = NULL;
      goto retry;
    }
  }

  hwloc_localeswitch_fini();
  hwloc_components_fini();
  return ret;
}

void hwloc_free_xmlbuffer(hwloc_topology_t topology __hwloc_attribute_unused, char *xmlbuffer)
{
  int force_nolibxml;

  assert(hwloc_nolibxml_callbacks); /* the core called components_init() for the topology */

  force_nolibxml = hwloc_nolibxml_export();
  if (!hwloc_libxml_callbacks || (hwloc_nolibxml_callbacks && force_nolibxml))
    hwloc_nolibxml_callbacks->free_buffer(xmlbuffer);
  else
    hwloc_libxml_callbacks->free_buffer(xmlbuffer);
}

void
hwloc_topology_set_userdata_export_callback(hwloc_topology_t topology,
					    void (*export)(void *reserved, struct hwloc_topology *topology, struct hwloc_obj *obj))
{
  topology->userdata_export_cb = export;
}

static void
hwloc__export_obj_userdata(hwloc__xml_export_state_t parentstate, int encoded,
			   const char *name, size_t length, const void *buffer, size_t encoded_length)
{
  struct hwloc__xml_export_state_s state;
  char tmp[255];
  parentstate->new_child(parentstate, &state, "userdata");
  if (name)
    state.new_prop(&state, "name", name);
  sprintf(tmp, "%lu", (unsigned long) length);
  state.new_prop(&state, "length", tmp);
  if (encoded)
    state.new_prop(&state, "encoding", "base64");
  if (encoded_length)
    state.add_content(&state, buffer, encoded ? encoded_length : length);
  state.end_object(&state, "userdata");
}

int
hwloc_export_obj_userdata(void *reserved,
			  struct hwloc_topology *topology, struct hwloc_obj *obj __hwloc_attribute_unused,
			  const char *name, const void *buffer, size_t length)
{
  hwloc__xml_export_state_t state = reserved;

  if (!buffer) {
    errno = EINVAL;
    return -1;
  }

  if ((name && hwloc__xml_export_check_buffer(name, strlen(name)) < 0)
      || hwloc__xml_export_check_buffer(buffer, length) < 0) {
    errno = EINVAL;
    return -1;
  }

  if (topology->userdata_not_decoded) {
    int encoded;
    size_t encoded_length;
    const char *realname;
    if (!strncmp(name, "normal", 6)) {
      encoded = 0;
      encoded_length = length;
    } else if (!strncmp(name, "base64", 6)) {
      encoded = 1;
      encoded_length = BASE64_ENCODED_LENGTH(length);
    } else
      assert(0);
    if (name[6] == ':')
      realname = name+7;
    else if (!strcmp(name+6, "-anon"))
      realname = NULL;
    else
      assert(0);
    hwloc__export_obj_userdata(state, encoded, realname, length, buffer, encoded_length);

  } else
    hwloc__export_obj_userdata(state, 0, name, length, buffer, length);

  return 0;
}

int
hwloc_export_obj_userdata_base64(void *reserved,
				 struct hwloc_topology *topology __hwloc_attribute_unused, struct hwloc_obj *obj __hwloc_attribute_unused,
				 const char *name, const void *buffer, size_t length)
{
  hwloc__xml_export_state_t state = reserved;
  size_t encoded_length;
  char *encoded_buffer;
  int ret __hwloc_attribute_unused;

  if (!buffer) {
    errno = EINVAL;
    return -1;
  }

  assert(!topology->userdata_not_decoded);

  if (name && hwloc__xml_export_check_buffer(name, strlen(name)) < 0) {
    errno = EINVAL;
    return -1;
  }

  encoded_length = BASE64_ENCODED_LENGTH(length);
  encoded_buffer = malloc(encoded_length+1);
  if (!encoded_buffer) {
    errno = ENOMEM;
    return -1;
  }

  ret = hwloc_encode_to_base64(buffer, length, encoded_buffer, encoded_length+1);
  assert(ret == (int) encoded_length);

  hwloc__export_obj_userdata(state, 1, name, length, encoded_buffer, encoded_length);

  free(encoded_buffer);
  return 0;
}

void
hwloc_topology_set_userdata_import_callback(hwloc_topology_t topology,
					    void (*import)(struct hwloc_topology *topology, struct hwloc_obj *obj, const char *name, const void *buffer, size_t length))
{
  topology->userdata_import_cb = import;
}

/***************************************
 ************ XML component ************
 ***************************************/

static void
hwloc_xml_backend_disable(struct hwloc_backend *backend)
{
  struct hwloc_xml_backend_data_s *data = backend->private_data;
  data->backend_exit(data);
  free(data->msgprefix);
  free(data);
}

static struct hwloc_backend *
hwloc_xml_component_instantiate(struct hwloc_disc_component *component,
				const void *_data1,
				const void *_data2,
				const void *_data3)
{
  struct hwloc_xml_backend_data_s *data;
  struct hwloc_backend *backend;
  const char *env;
  int force_nolibxml;
  const char * xmlpath = (const char *) _data1;
  const char * xmlbuffer = (const char *) _data2;
  int xmlbuflen = (int)(uintptr_t) _data3;
  const char *basename;
  int err;

  assert(hwloc_nolibxml_callbacks); /* the core called components_init() for the component's topology */

  if (!xmlpath && !xmlbuffer) {
    env = getenv("HWLOC_XMLFILE");
    if (env) {
      /* 'xml' was given in HWLOC_COMPONENTS without a filename */
      xmlpath = env;
    } else {
      errno = EINVAL;
      goto out;
    }
  }

  backend = hwloc_backend_alloc(component);
  if (!backend)
    goto out;

  data = malloc(sizeof(*data));
  if (!data) {
    errno = ENOMEM;
    goto out_with_backend;
  }

  backend->private_data = data;
  backend->discover = hwloc_look_xml;
  backend->disable = hwloc_xml_backend_disable;
  backend->is_thissystem = 0;

  if (xmlpath) {
    basename = strrchr(xmlpath, '/');
    if (basename)
      basename++;
    else
      basename = xmlpath;
  } else {
    basename = "xmlbuffer";
  }
  data->msgprefix = strdup(basename);

  force_nolibxml = hwloc_nolibxml_import();
retry:
  if (!hwloc_libxml_callbacks || (hwloc_nolibxml_callbacks && force_nolibxml))
    err = hwloc_nolibxml_callbacks->backend_init(data, xmlpath, xmlbuffer, xmlbuflen);
  else {
    err = hwloc_libxml_callbacks->backend_init(data, xmlpath, xmlbuffer, xmlbuflen);
    if (err < 0 && errno == ENOSYS) {
      hwloc_libxml_callbacks = NULL;
      goto retry;
    }
  }
  if (err < 0)
    goto out_with_data;

  return backend;

 out_with_data:
  free(data->msgprefix);
  free(data);
 out_with_backend:
  free(backend);
 out:
  return NULL;
}

static struct hwloc_disc_component hwloc_xml_disc_component = {
  HWLOC_DISC_COMPONENT_TYPE_GLOBAL,
  "xml",
  ~0,
  hwloc_xml_component_instantiate,
  30,
  NULL
};

const struct hwloc_component hwloc_xml_component = {
  HWLOC_COMPONENT_ABI,
  NULL, NULL,
  HWLOC_COMPONENT_TYPE_DISC,
  0,
  &hwloc_xml_disc_component
};
