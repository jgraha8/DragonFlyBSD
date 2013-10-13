/*
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2008 Red Hat Inc.
 *
 * DRM core CRTC related functions
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 *
 * Authors:
 *      Keith Packard
 *	Eric Anholt <eric@anholt.net>
 *      Dave Airlie <airlied@linux.ie>
 *      Jesse Barnes <jesse.barnes@intel.com>
 *
 * $FreeBSD: src/sys/dev/drm2/drm_crtc.c,v 1.1 2012/05/22 11:07:44 kib Exp $
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <sys/limits.h>

/* Avoid boilerplate.  I'm tired of typing. */
#define DRM_ENUM_NAME_FN(fnname, list)				\
	char *fnname(int val)					\
	{							\
		int i;						\
		for (i = 0; i < DRM_ARRAY_SIZE(list); i++) {	\
			if (list[i].type == val)		\
				return list[i].name;		\
		}						\
		return "(unknown)";				\
	}

/*
 * Global properties
 */
static struct drm_prop_enum_list drm_dpms_enum_list[] =
{	{ DRM_MODE_DPMS_ON, "On" },
	{ DRM_MODE_DPMS_STANDBY, "Standby" },
	{ DRM_MODE_DPMS_SUSPEND, "Suspend" },
	{ DRM_MODE_DPMS_OFF, "Off" }
};

DRM_ENUM_NAME_FN(drm_get_dpms_name, drm_dpms_enum_list)

/*
 * Optional properties
 */
static struct drm_prop_enum_list drm_scaling_mode_enum_list[] =
{
	{ DRM_MODE_SCALE_NONE, "None" },
	{ DRM_MODE_SCALE_FULLSCREEN, "Full" },
	{ DRM_MODE_SCALE_CENTER, "Center" },
	{ DRM_MODE_SCALE_ASPECT, "Full aspect" },
};

static struct drm_prop_enum_list drm_dithering_mode_enum_list[] =
{
	{ DRM_MODE_DITHERING_OFF, "Off" },
	{ DRM_MODE_DITHERING_ON, "On" },
	{ DRM_MODE_DITHERING_AUTO, "Automatic" },
};

/*
 * Non-global properties, but "required" for certain connectors.
 */
static struct drm_prop_enum_list drm_dvi_i_select_enum_list[] =
{
	{ DRM_MODE_SUBCONNECTOR_Automatic, "Automatic" }, /* DVI-I and TV-out */
	{ DRM_MODE_SUBCONNECTOR_DVID,      "DVI-D"     }, /* DVI-I  */
	{ DRM_MODE_SUBCONNECTOR_DVIA,      "DVI-A"     }, /* DVI-I  */
};

DRM_ENUM_NAME_FN(drm_get_dvi_i_select_name, drm_dvi_i_select_enum_list)

static struct drm_prop_enum_list drm_dvi_i_subconnector_enum_list[] =
{
	{ DRM_MODE_SUBCONNECTOR_Unknown,   "Unknown"   }, /* DVI-I and TV-out */
	{ DRM_MODE_SUBCONNECTOR_DVID,      "DVI-D"     }, /* DVI-I  */
	{ DRM_MODE_SUBCONNECTOR_DVIA,      "DVI-A"     }, /* DVI-I  */
};

DRM_ENUM_NAME_FN(drm_get_dvi_i_subconnector_name,
		 drm_dvi_i_subconnector_enum_list)

static struct drm_prop_enum_list drm_tv_select_enum_list[] =
{
	{ DRM_MODE_SUBCONNECTOR_Automatic, "Automatic" }, /* DVI-I and TV-out */
	{ DRM_MODE_SUBCONNECTOR_Composite, "Composite" }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_SVIDEO,    "SVIDEO"    }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_Component, "Component" }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_SCART,     "SCART"     }, /* TV-out */
};

DRM_ENUM_NAME_FN(drm_get_tv_select_name, drm_tv_select_enum_list)

static struct drm_prop_enum_list drm_tv_subconnector_enum_list[] =
{
	{ DRM_MODE_SUBCONNECTOR_Unknown,   "Unknown"   }, /* DVI-I and TV-out */
	{ DRM_MODE_SUBCONNECTOR_Composite, "Composite" }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_SVIDEO,    "SVIDEO"    }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_Component, "Component" }, /* TV-out */
	{ DRM_MODE_SUBCONNECTOR_SCART,     "SCART"     }, /* TV-out */
};

DRM_ENUM_NAME_FN(drm_get_tv_subconnector_name,
		 drm_tv_subconnector_enum_list)

static struct drm_prop_enum_list drm_dirty_info_enum_list[] = {
	{ DRM_MODE_DIRTY_OFF,      "Off"      },
	{ DRM_MODE_DIRTY_ON,       "On"       },
	{ DRM_MODE_DIRTY_ANNOTATE, "Annotate" },
};

DRM_ENUM_NAME_FN(drm_get_dirty_info_name,
		 drm_dirty_info_enum_list)

struct drm_conn_prop_enum_list {
	int type;
	char *name;
	int count;
};

/*
 * Connector and encoder types.
 */
static struct drm_conn_prop_enum_list drm_connector_enum_list[] =
{	{ DRM_MODE_CONNECTOR_Unknown, "Unknown", 0 },
	{ DRM_MODE_CONNECTOR_VGA, "VGA", 0 },
	{ DRM_MODE_CONNECTOR_DVII, "DVI-I", 0 },
	{ DRM_MODE_CONNECTOR_DVID, "DVI-D", 0 },
	{ DRM_MODE_CONNECTOR_DVIA, "DVI-A", 0 },
	{ DRM_MODE_CONNECTOR_Composite, "Composite", 0 },
	{ DRM_MODE_CONNECTOR_SVIDEO, "SVIDEO", 0 },
	{ DRM_MODE_CONNECTOR_LVDS, "LVDS", 0 },
	{ DRM_MODE_CONNECTOR_Component, "Component", 0 },
	{ DRM_MODE_CONNECTOR_9PinDIN, "DIN", 0 },
	{ DRM_MODE_CONNECTOR_DisplayPort, "DP", 0 },
	{ DRM_MODE_CONNECTOR_HDMIA, "HDMI-A", 0 },
	{ DRM_MODE_CONNECTOR_HDMIB, "HDMI-B", 0 },
	{ DRM_MODE_CONNECTOR_TV, "TV", 0 },
	{ DRM_MODE_CONNECTOR_eDP, "eDP", 0 },
};

static struct drm_prop_enum_list drm_encoder_enum_list[] =
{	{ DRM_MODE_ENCODER_NONE, "None" },
	{ DRM_MODE_ENCODER_DAC, "DAC" },
	{ DRM_MODE_ENCODER_TMDS, "TMDS" },
	{ DRM_MODE_ENCODER_LVDS, "LVDS" },
	{ DRM_MODE_ENCODER_TVDAC, "TV" },
};

static void drm_property_destroy_blob(struct drm_device *dev,
			       struct drm_property_blob *blob);

char *drm_get_encoder_name(struct drm_encoder *encoder)
{
	static char buf[32];

	ksnprintf(buf, 32, "%s-%d",
		 drm_encoder_enum_list[encoder->encoder_type].name,
		 encoder->base.id);
	return buf;
}

char *drm_get_connector_name(struct drm_connector *connector)
{
	static char buf[32];

	ksnprintf(buf, 32, "%s-%d",
		 drm_connector_enum_list[connector->connector_type].name,
		 connector->connector_type_id);
	return buf;
}

char *drm_get_connector_status_name(enum drm_connector_status status)
{
	if (status == connector_status_connected)
		return "connected";
	else if (status == connector_status_disconnected)
		return "disconnected";
	else
		return "unknown";
}

/**
 * drm_mode_object_get - allocate a new identifier
 * @dev: DRM device
 * @ptr: object pointer, used to generate unique ID
 * @type: object type
 *
 * LOCKING:
 *
 * Create a unique identifier based on @ptr in @dev's identifier space.  Used
 * for tracking modes, CRTCs and connectors.
 *
 * RETURNS:
 * New unique (relative to other objects in @dev) integer identifier for the
 * object.
 */
static int drm_mode_object_get(struct drm_device *dev,
			       struct drm_mode_object *obj, uint32_t obj_type)
{
	int new_id = 0;
	int ret;

again:
	if (idr_pre_get(&dev->mode_config.crtc_idr, GFP_KERNEL) == 0) {
		DRM_ERROR("Ran out memory getting a mode number\n");
		return -ENOMEM;
	}

	spin_lock(&dev->mode_config.idr_mutex);
	ret = idr_get_new_above(&dev->mode_config.crtc_idr, obj, 1, &new_id);
	spin_unlock(&dev->mode_config.idr_mutex);
	if (ret == -EAGAIN)
		goto again;
	else if (ret)
		return ret;

	obj->id = new_id;
	obj->type = obj_type;
	return 0;
}

/**
 * drm_mode_object_put - free an identifer
 * @dev: DRM device
 * @id: ID to free
 *
 * LOCKING:
 * Caller must hold DRM mode_config lock.
 *
 * Free @id from @dev's unique identifier pool.
 */
static void drm_mode_object_put(struct drm_device *dev,
				struct drm_mode_object *object)
{
	spin_lock(&dev->mode_config.idr_mutex);
	idr_remove(&dev->mode_config.crtc_idr, object->id);
	spin_unlock(&dev->mode_config.idr_mutex);
}

struct drm_mode_object *drm_mode_object_find(struct drm_device *dev,
		uint32_t id, uint32_t type)
{
	struct drm_mode_object *obj = NULL;

	spin_lock(&dev->mode_config.idr_mutex);
	obj = idr_find(&dev->mode_config.crtc_idr, id);
	if (!obj || (obj->type != type) || (obj->id != id))
		obj = NULL;
	spin_unlock(&dev->mode_config.idr_mutex);

	return obj;
}

/**
 * drm_framebuffer_init - initialize a framebuffer
 * @dev: DRM device
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Allocates an ID for the framebuffer's parent mode object, sets its mode
 * functions & device file and adds it to the master fd list.
 *
 * RETURNS:
 * Zero on success, error code on failure.
 */
int drm_framebuffer_init(struct drm_device *dev, struct drm_framebuffer *fb,
			 const struct drm_framebuffer_funcs *funcs)
{
	int ret;

	DRM_MODE_CONFIG_ASSERT_LOCKED(dev);

	ret = drm_mode_object_get(dev, &fb->base, DRM_MODE_OBJECT_FB);
	if (ret)
		return ret;

	fb->dev = dev;
	fb->funcs = funcs;
	dev->mode_config.num_fb++;
	list_add(&fb->head, &dev->mode_config.fb_list);

	return 0;
}

/**
 * drm_framebuffer_cleanup - remove a framebuffer object
 * @fb: framebuffer to remove
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Scans all the CRTCs in @dev's mode_config.  If they're using @fb, removes
 * it, setting it to NULL.
 */
void drm_framebuffer_cleanup(struct drm_framebuffer *fb)
{
	struct drm_device *dev = fb->dev;
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	struct drm_mode_set set;
	int ret;

	DRM_MODE_CONFIG_ASSERT_LOCKED(dev);

	/* remove from any CRTC */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (crtc->fb == fb) {
			/* should turn off the crtc */
			memset(&set, 0, sizeof(struct drm_mode_set));
			set.crtc = crtc;
			set.fb = NULL;
			ret = crtc->funcs->set_config(&set);
			if (ret)
				DRM_ERROR("failed to reset crtc %p when fb was deleted\n", crtc);
		}
	}

	list_for_each_entry(plane, &dev->mode_config.plane_list, head) {
		if (plane->fb == fb) {
			/* should turn off the crtc */
			ret = plane->funcs->disable_plane(plane);
			if (ret)
				DRM_ERROR("failed to disable plane with busy fb\n");
			/* disconnect the plane from the fb and crtc: */
			plane->fb = NULL;
			plane->crtc = NULL;
		}
	}

	drm_mode_object_put(dev, &fb->base);
	list_del(&fb->head);
	dev->mode_config.num_fb--;
}

/**
 * drm_crtc_init - Initialise a new CRTC object
 * @dev: DRM device
 * @crtc: CRTC object to init
 * @funcs: callbacks for the new CRTC
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Inits a new object created as base part of an driver crtc object.
 *
 * RETURNS:
 * Zero on success, error code on failure.
 */
int drm_crtc_init(struct drm_device *dev, struct drm_crtc *crtc,
		   const struct drm_crtc_funcs *funcs)
{
	int ret;

	crtc->dev = dev;
	crtc->funcs = funcs;

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	ret = drm_mode_object_get(dev, &crtc->base, DRM_MODE_OBJECT_CRTC);
	if (ret)
		goto out;

	list_add_tail(&crtc->head, &dev->mode_config.crtc_list);
	dev->mode_config.num_crtc++;
out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);

	return ret;
}

/**
 * drm_crtc_cleanup - Cleans up the core crtc usage.
 * @crtc: CRTC to cleanup
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Cleanup @crtc. Removes from drm modesetting space
 * does NOT free object, caller does that.
 */
void drm_crtc_cleanup(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;

	DRM_MODE_CONFIG_ASSERT_LOCKED(dev);

	if (crtc->gamma_store) {
		drm_free(crtc->gamma_store, DRM_MEM_KMS);
		crtc->gamma_store = NULL;
	}

	drm_mode_object_put(dev, &crtc->base);
	list_del(&crtc->head);
	dev->mode_config.num_crtc--;
}

/**
 * drm_mode_probed_add - add a mode to a connector's probed mode list
 * @connector: connector the new mode
 * @mode: mode data
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Add @mode to @connector's mode list for later use.
 */
void drm_mode_probed_add(struct drm_connector *connector,
			 struct drm_display_mode *mode)
{

	DRM_MODE_CONFIG_ASSERT_LOCKED(connector->dev);

	list_add(&mode->head, &connector->probed_modes);
}

/**
 * drm_mode_remove - remove and free a mode
 * @connector: connector list to modify
 * @mode: mode to remove
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Remove @mode from @connector's mode list, then free it.
 */
void drm_mode_remove(struct drm_connector *connector,
		     struct drm_display_mode *mode)
{

	DRM_MODE_CONFIG_ASSERT_LOCKED(connector->dev);

	list_del(&mode->head);
	drm_mode_destroy(connector->dev, mode);
}

/**
 * drm_connector_init - Init a preallocated connector
 * @dev: DRM device
 * @connector: the connector to init
 * @funcs: callbacks for this connector
 * @name: user visible name of the connector
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Initialises a preallocated connector. Connectors should be
 * subclassed as part of driver connector objects.
 *
 * RETURNS:
 * Zero on success, error code on failure.
 */
int drm_connector_init(struct drm_device *dev,
		       struct drm_connector *connector,
		       const struct drm_connector_funcs *funcs,
		       int connector_type)
{
	int ret;

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);

	ret = drm_mode_object_get(dev, &connector->base, DRM_MODE_OBJECT_CONNECTOR);
	if (ret)
		goto out;

	connector->dev = dev;
	connector->funcs = funcs;
	connector->connector_type = connector_type;
	connector->connector_type_id =
		++drm_connector_enum_list[connector_type].count; /* TODO */
	INIT_LIST_HEAD(&connector->user_modes);
	INIT_LIST_HEAD(&connector->probed_modes);
	INIT_LIST_HEAD(&connector->modes);
	connector->edid_blob_ptr = NULL;

	list_add_tail(&connector->head, &dev->mode_config.connector_list);
	dev->mode_config.num_connector++;

	drm_connector_attach_property(connector,
				      dev->mode_config.edid_property, 0);

	drm_connector_attach_property(connector,
				      dev->mode_config.dpms_property, 0);

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);

	return ret;
}

/**
 * drm_connector_cleanup - cleans up an initialised connector
 * @connector: connector to cleanup
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Cleans up the connector but doesn't free the object.
 */
void drm_connector_cleanup(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode, *t;

	list_for_each_entry_safe(mode, t, &connector->probed_modes, head)
		drm_mode_remove(connector, mode);

	list_for_each_entry_safe(mode, t, &connector->modes, head)
		drm_mode_remove(connector, mode);

	list_for_each_entry_safe(mode, t, &connector->user_modes, head)
		drm_mode_remove(connector, mode);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	if (connector->edid_blob_ptr)
		drm_property_destroy_blob(dev, connector->edid_blob_ptr);
	drm_mode_object_put(dev, &connector->base);
	list_del(&connector->head);
	dev->mode_config.num_connector--;
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
}

int drm_encoder_init(struct drm_device *dev,
		     struct drm_encoder *encoder,
		     const struct drm_encoder_funcs *funcs,
		     int encoder_type)
{
	int ret;

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);

	ret = drm_mode_object_get(dev, &encoder->base, DRM_MODE_OBJECT_ENCODER);
	if (ret)
		goto out;

	encoder->dev = dev;
	encoder->encoder_type = encoder_type;
	encoder->funcs = funcs;

	list_add_tail(&encoder->head, &dev->mode_config.encoder_list);
	dev->mode_config.num_encoder++;

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);

	return ret;
}

void drm_encoder_cleanup(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	drm_mode_object_put(dev, &encoder->base);
	list_del(&encoder->head);
	dev->mode_config.num_encoder--;
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
}

int drm_plane_init(struct drm_device *dev, struct drm_plane *plane,
		   unsigned long possible_crtcs,
		   const struct drm_plane_funcs *funcs,
		   const uint32_t *formats, uint32_t format_count,
		   bool priv)
{
	int ret;

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);

	ret = drm_mode_object_get(dev, &plane->base, DRM_MODE_OBJECT_PLANE);
	if (ret)
		goto out;

	plane->dev = dev;
	plane->funcs = funcs;
	plane->format_types = kmalloc(sizeof(uint32_t) * format_count,
	    DRM_MEM_KMS, M_WAITOK);

	memcpy(plane->format_types, formats, format_count * sizeof(uint32_t));
	plane->format_count = format_count;
	plane->possible_crtcs = possible_crtcs;

	/* private planes are not exposed to userspace, but depending on
	 * display hardware, might be convenient to allow sharing programming
	 * for the scanout engine with the crtc implementation.
	 */
	if (!priv) {
		list_add_tail(&plane->head, &dev->mode_config.plane_list);
		dev->mode_config.num_plane++;
	} else {
		INIT_LIST_HEAD(&plane->head);
	}

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);

	return ret;
}

void drm_plane_cleanup(struct drm_plane *plane)
{
	struct drm_device *dev = plane->dev;

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	drm_free(plane->format_types, DRM_MEM_KMS);
	drm_mode_object_put(dev, &plane->base);
	/* if not added to a list, it must be a private plane */
	if (!list_empty(&plane->head)) {
		list_del(&plane->head);
		dev->mode_config.num_plane--;
	}
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
}

/**
 * drm_mode_create - create a new display mode
 * @dev: DRM device
 *
 * LOCKING:
 * Caller must hold DRM mode_config lock.
 *
 * Create a new drm_display_mode, give it an ID, and return it.
 *
 * RETURNS:
 * Pointer to new mode on success, NULL on error.
 */
struct drm_display_mode *drm_mode_create(struct drm_device *dev)
{
	struct drm_display_mode *nmode;

	nmode = kmalloc(sizeof(struct drm_display_mode), DRM_MEM_KMS,
	    M_WAITOK | M_ZERO);

	if (drm_mode_object_get(dev, &nmode->base, DRM_MODE_OBJECT_MODE)) {
		drm_free(nmode, DRM_MEM_KMS);
		return (NULL);
	}
	return nmode;
}

/**
 * drm_mode_destroy - remove a mode
 * @dev: DRM device
 * @mode: mode to remove
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Free @mode's unique identifier, then free it.
 */
void drm_mode_destroy(struct drm_device *dev, struct drm_display_mode *mode)
{
	if (!mode)
		return;

	drm_mode_object_put(dev, &mode->base);

	drm_free(mode, DRM_MEM_KMS);
}

static int drm_mode_create_standard_connector_properties(struct drm_device *dev)
{
	struct drm_property *edid;
	struct drm_property *dpms;

	/*
	 * Standard properties (apply to all connectors)
	 */
	edid = drm_property_create(dev, DRM_MODE_PROP_BLOB |
				   DRM_MODE_PROP_IMMUTABLE,
				   "EDID", 0);
	dev->mode_config.edid_property = edid;

	dpms = drm_property_create_enum(dev, 0,
				   "DPMS", drm_dpms_enum_list,
				    DRM_ARRAY_SIZE(drm_dpms_enum_list));
	dev->mode_config.dpms_property = dpms;

	return 0;
}

/**
 * drm_mode_create_dvi_i_properties - create DVI-I specific connector properties
 * @dev: DRM device
 *
 * Called by a driver the first time a DVI-I connector is made.
 */
int drm_mode_create_dvi_i_properties(struct drm_device *dev)
{
	struct drm_property *dvi_i_selector;
	struct drm_property *dvi_i_subconnector;

	if (dev->mode_config.dvi_i_select_subconnector_property)
		return 0;

	dvi_i_selector =
		drm_property_create_enum(dev, 0,
				    "select subconnector",
				    drm_dvi_i_select_enum_list,
				    DRM_ARRAY_SIZE(drm_dvi_i_select_enum_list));
	dev->mode_config.dvi_i_select_subconnector_property = dvi_i_selector;

	dvi_i_subconnector = drm_property_create_enum(dev, DRM_MODE_PROP_IMMUTABLE,
				    "subconnector",
				    drm_dvi_i_subconnector_enum_list,
				    DRM_ARRAY_SIZE(drm_dvi_i_subconnector_enum_list));
	dev->mode_config.dvi_i_subconnector_property = dvi_i_subconnector;

	return 0;
}

/**
 * drm_create_tv_properties - create TV specific connector properties
 * @dev: DRM device
 * @num_modes: number of different TV formats (modes) supported
 * @modes: array of pointers to strings containing name of each format
 *
 * Called by a driver's TV initialization routine, this function creates
 * the TV specific connector properties for a given device.  Caller is
 * responsible for allocating a list of format names and passing them to
 * this routine.
 */
int drm_mode_create_tv_properties(struct drm_device *dev, int num_modes,
				  char *modes[])
{
	struct drm_property *tv_selector;
	struct drm_property *tv_subconnector;
	int i;

	if (dev->mode_config.tv_select_subconnector_property)
		return 0;

	/*
	 * Basic connector properties
	 */
	tv_selector = drm_property_create_enum(dev, 0,
					  "select subconnector",
					  drm_tv_select_enum_list,
					  DRM_ARRAY_SIZE(drm_tv_select_enum_list));
	dev->mode_config.tv_select_subconnector_property = tv_selector;

	tv_subconnector =
		drm_property_create_enum(dev, DRM_MODE_PROP_IMMUTABLE,
				    "subconnector",
				    drm_tv_subconnector_enum_list,
 				    DRM_ARRAY_SIZE(drm_tv_subconnector_enum_list));
	dev->mode_config.tv_subconnector_property = tv_subconnector;

	/*
	 * Other, TV specific properties: margins & TV modes.
	 */
	dev->mode_config.tv_left_margin_property =
		drm_property_create_range(dev, 0, "left margin", 0, 100);

	dev->mode_config.tv_right_margin_property =
		drm_property_create_range(dev, 0, "right margin", 0, 100);

	dev->mode_config.tv_top_margin_property =
		drm_property_create_range(dev, 0, "top margin", 0, 100);

	dev->mode_config.tv_bottom_margin_property =
		drm_property_create_range(dev, 0, "bottom margin", 0, 100);

	dev->mode_config.tv_mode_property =
		drm_property_create(dev, DRM_MODE_PROP_ENUM,
				    "mode", num_modes);
	for (i = 0; i < num_modes; i++)
		drm_property_add_enum(dev->mode_config.tv_mode_property, i,
				      i, modes[i]);

	dev->mode_config.tv_brightness_property =
		drm_property_create_range(dev, 0, "brightness", 0, 100);

	dev->mode_config.tv_contrast_property =
		drm_property_create_range(dev, 0, "contrast", 0, 100);

	dev->mode_config.tv_flicker_reduction_property =
		drm_property_create_range(dev, 0, "flicker reduction", 0, 100);

	dev->mode_config.tv_overscan_property =
		drm_property_create_range(dev, 0, "overscan", 0, 100);

	dev->mode_config.tv_saturation_property =
		drm_property_create_range(dev, 0, "saturation", 0, 100);

	dev->mode_config.tv_hue_property =
		drm_property_create_range(dev, 0, "hue", 0, 100);

	return 0;
}

/**
 * drm_mode_create_scaling_mode_property - create scaling mode property
 * @dev: DRM device
 *
 * Called by a driver the first time it's needed, must be attached to desired
 * connectors.
 */
int drm_mode_create_scaling_mode_property(struct drm_device *dev)
{
	struct drm_property *scaling_mode;

	if (dev->mode_config.scaling_mode_property)
		return 0;

	scaling_mode =
		drm_property_create_enum(dev, 0, "scaling mode",
				drm_scaling_mode_enum_list,
				    DRM_ARRAY_SIZE(drm_scaling_mode_enum_list));

	dev->mode_config.scaling_mode_property = scaling_mode;

	return 0;
}

/**
 * drm_mode_create_dithering_property - create dithering property
 * @dev: DRM device
 *
 * Called by a driver the first time it's needed, must be attached to desired
 * connectors.
 */
int drm_mode_create_dithering_property(struct drm_device *dev)
{
	struct drm_property *dithering_mode;

	if (dev->mode_config.dithering_mode_property)
		return 0;

	dithering_mode =
		drm_property_create_enum(dev, 0, "dithering",
				drm_dithering_mode_enum_list,
				    DRM_ARRAY_SIZE(drm_dithering_mode_enum_list));
	dev->mode_config.dithering_mode_property = dithering_mode;

	return 0;
}

/**
 * drm_mode_create_dirty_property - create dirty property
 * @dev: DRM device
 *
 * Called by a driver the first time it's needed, must be attached to desired
 * connectors.
 */
int drm_mode_create_dirty_info_property(struct drm_device *dev)
{
	struct drm_property *dirty_info;

	if (dev->mode_config.dirty_info_property)
		return 0;

	dirty_info =
		drm_property_create_enum(dev, DRM_MODE_PROP_IMMUTABLE,
				    "dirty",
				    drm_dirty_info_enum_list,
				    DRM_ARRAY_SIZE(drm_dirty_info_enum_list));
	dev->mode_config.dirty_info_property = dirty_info;

	return 0;
}

/**
 * drm_mode_config_init - initialize DRM mode_configuration structure
 * @dev: DRM device
 *
 * LOCKING:
 * None, should happen single threaded at init time.
 *
 * Initialize @dev's mode_config structure, used for tracking the graphics
 * configuration of @dev.
 */
void drm_mode_config_init(struct drm_device *dev)
{
	lockinit(&dev->mode_config.lock, "kmslk", 0, LK_CANRECURSE);
	spin_init(&dev->mode_config.idr_mutex);
	INIT_LIST_HEAD(&dev->mode_config.fb_list);
	INIT_LIST_HEAD(&dev->mode_config.crtc_list);
	INIT_LIST_HEAD(&dev->mode_config.connector_list);
	INIT_LIST_HEAD(&dev->mode_config.encoder_list);
	INIT_LIST_HEAD(&dev->mode_config.property_list);
	INIT_LIST_HEAD(&dev->mode_config.property_blob_list);
	INIT_LIST_HEAD(&dev->mode_config.plane_list);
	idr_init(&dev->mode_config.crtc_idr);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	drm_mode_create_standard_connector_properties(dev);
	lockmgr(&dev->mode_config.lock, LK_RELEASE);

	/* Just to be sure */
	dev->mode_config.num_fb = 0;
	dev->mode_config.num_connector = 0;
	dev->mode_config.num_crtc = 0;
	dev->mode_config.num_encoder = 0;
}

static int
drm_mode_group_init(struct drm_device *dev, struct drm_mode_group *group)
{
	uint32_t total_objects = 0;

	total_objects += dev->mode_config.num_crtc;
	total_objects += dev->mode_config.num_connector;
	total_objects += dev->mode_config.num_encoder;

	group->id_list = kmalloc(total_objects * sizeof(uint32_t),
	    DRM_MEM_KMS, M_WAITOK | M_ZERO);

	group->num_crtcs = 0;
	group->num_connectors = 0;
	group->num_encoders = 0;
	return 0;
}

int drm_mode_group_init_legacy_group(struct drm_device *dev,
				     struct drm_mode_group *group)
{
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int ret;

	if ((ret = drm_mode_group_init(dev, group)))
		return ret;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		group->id_list[group->num_crtcs++] = crtc->base.id;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head)
		group->id_list[group->num_crtcs + group->num_encoders++] =
		encoder->base.id;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		group->id_list[group->num_crtcs + group->num_encoders +
			       group->num_connectors++] = connector->base.id;

	return 0;
}

/**
 * drm_mode_config_cleanup - free up DRM mode_config info
 * @dev: DRM device
 *
 * LOCKING:
 * Caller must hold mode config lock.
 *
 * Free up all the connectors and CRTCs associated with this DRM device, then
 * free up the framebuffers and associated buffer objects.
 *
 * FIXME: cleanup any dangling user buffer objects too
 */
void drm_mode_config_cleanup(struct drm_device *dev)
{
	struct drm_connector *connector, *ot;
	struct drm_crtc *crtc, *ct;
	struct drm_encoder *encoder, *enct;
	struct drm_framebuffer *fb, *fbt;
	struct drm_property *property, *pt;
	struct drm_plane *plane, *plt;

	list_for_each_entry_safe(encoder, enct, &dev->mode_config.encoder_list,
				 head) {
		encoder->funcs->destroy(encoder);
	}

	list_for_each_entry_safe(connector, ot,
				 &dev->mode_config.connector_list, head) {
		connector->funcs->destroy(connector);
	}

	list_for_each_entry_safe(property, pt, &dev->mode_config.property_list,
				 head) {
		drm_property_destroy(dev, property);
	}

	list_for_each_entry_safe(fb, fbt, &dev->mode_config.fb_list, head) {
		fb->funcs->destroy(fb);
	}

	list_for_each_entry_safe(crtc, ct, &dev->mode_config.crtc_list, head) {
		crtc->funcs->destroy(crtc);
	}

	list_for_each_entry_safe(plane, plt, &dev->mode_config.plane_list,
				 head) {
		plane->funcs->destroy(plane);
	}

	list_for_each_entry_safe(crtc, ct, &dev->mode_config.crtc_list, head) {
		crtc->funcs->destroy(crtc);
	}

	idr_remove_all(&dev->mode_config.crtc_idr);
	idr_destroy(&dev->mode_config.crtc_idr);
}

/**
 * drm_crtc_convert_to_umode - convert a drm_display_mode into a modeinfo
 * @out: drm_mode_modeinfo struct to return to the user
 * @in: drm_display_mode to use
 *
 * LOCKING:
 * None.
 *
 * Convert a drm_display_mode into a drm_mode_modeinfo structure to return to
 * the user.
 */
static void drm_crtc_convert_to_umode(struct drm_mode_modeinfo *out,
				      const struct drm_display_mode *in)
{
	if (in->hdisplay > USHRT_MAX || in->hsync_start > USHRT_MAX ||
	    in->hsync_end > USHRT_MAX || in->htotal > USHRT_MAX ||
	    in->hskew > USHRT_MAX || in->vdisplay > USHRT_MAX ||
	    in->vsync_start > USHRT_MAX || in->vsync_end > USHRT_MAX ||
	    in->vtotal > USHRT_MAX || in->vscan > USHRT_MAX)
		kprintf("timing values too large for mode info\n");

	out->clock = in->clock;
	out->hdisplay = in->hdisplay;
	out->hsync_start = in->hsync_start;
	out->hsync_end = in->hsync_end;
	out->htotal = in->htotal;
	out->hskew = in->hskew;
	out->vdisplay = in->vdisplay;
	out->vsync_start = in->vsync_start;
	out->vsync_end = in->vsync_end;
	out->vtotal = in->vtotal;
	out->vscan = in->vscan;
	out->vrefresh = in->vrefresh;
	out->flags = in->flags;
	out->type = in->type;
	strncpy(out->name, in->name, DRM_DISPLAY_MODE_LEN);
	out->name[DRM_DISPLAY_MODE_LEN-1] = 0;
}

/**
 * drm_crtc_convert_to_umode - convert a modeinfo into a drm_display_mode
 * @out: drm_display_mode to return to the user
 * @in: drm_mode_modeinfo to use
 *
 * LOCKING:
 * None.
 *
 * Convert a drm_mode_modeinfo into a drm_display_mode structure to return to
 * the caller.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
static int drm_crtc_convert_umode(struct drm_display_mode *out,
				  const struct drm_mode_modeinfo *in)
{
	if (in->clock > INT_MAX || in->vrefresh > INT_MAX)
		return ERANGE;

	out->clock = in->clock;
	out->hdisplay = in->hdisplay;
	out->hsync_start = in->hsync_start;
	out->hsync_end = in->hsync_end;
	out->htotal = in->htotal;
	out->hskew = in->hskew;
	out->vdisplay = in->vdisplay;
	out->vsync_start = in->vsync_start;
	out->vsync_end = in->vsync_end;
	out->vtotal = in->vtotal;
	out->vscan = in->vscan;
	out->vrefresh = in->vrefresh;
	out->flags = in->flags;
	out->type = in->type;
	strncpy(out->name, in->name, DRM_DISPLAY_MODE_LEN);
	out->name[DRM_DISPLAY_MODE_LEN-1] = 0;

	return 0;
}

/**
 * drm_mode_getresources - get graphics configuration
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Construct a set of configuration description structures and return
 * them to the user, including CRTC, connector and framebuffer configuration.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_getresources(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_mode_card_res *card_res = data;
	struct list_head *lh;
	struct drm_framebuffer *fb;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	int ret = 0;
	int connector_count = 0;
	int crtc_count = 0;
	int fb_count = 0;
	int encoder_count = 0;
	int copied = 0, i;
	uint32_t __user *fb_id;
	uint32_t __user *crtc_id;
	uint32_t __user *connector_id;
	uint32_t __user *encoder_id;
	struct drm_mode_group *mode_group;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return (EINVAL);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);

	/*
	 * For the non-control nodes we need to limit the list of resources
	 * by IDs in the group list for this node
	 */
	list_for_each(lh, &file_priv->fbs)
		fb_count++;

#if 1
	mode_group = NULL; /* XXXKIB */
	if (1 || file_priv->master) {
#else
	mode_group = &file_priv->masterp->minor->mode_group;
	if (file_priv->masterp->minor->type == DRM_MINOR_CONTROL) {
#endif

		list_for_each(lh, &dev->mode_config.crtc_list)
			crtc_count++;

		list_for_each(lh, &dev->mode_config.connector_list)
			connector_count++;

		list_for_each(lh, &dev->mode_config.encoder_list)
			encoder_count++;
	} else {

		crtc_count = mode_group->num_crtcs;
		connector_count = mode_group->num_connectors;
		encoder_count = mode_group->num_encoders;
	}

	card_res->max_height = dev->mode_config.max_height;
	card_res->min_height = dev->mode_config.min_height;
	card_res->max_width = dev->mode_config.max_width;
	card_res->min_width = dev->mode_config.min_width;

	/* handle this in 4 parts */
	/* FBs */
	if (card_res->count_fbs >= fb_count) {
		copied = 0;
		fb_id = (uint32_t *)(uintptr_t)card_res->fb_id_ptr;
		list_for_each_entry(fb, &file_priv->fbs, filp_head) {
			if (copyout(&fb->base.id, fb_id + copied,
			    sizeof(uint32_t))) {
				ret = EFAULT;
				goto out;
			}
			copied++;
		}
	}
	card_res->count_fbs = fb_count;

	/* CRTCs */
	if (card_res->count_crtcs >= crtc_count) {
		copied = 0;
		crtc_id = (uint32_t *)(uintptr_t)card_res->crtc_id_ptr;
#if 1
		if (1 || file_priv->master) {
#else
		if (file_priv->masterp->minor->type == DRM_MINOR_CONTROL) {
#endif
			list_for_each_entry(crtc, &dev->mode_config.crtc_list,
					    head) {
				DRM_DEBUG_KMS("[CRTC:%d]\n", crtc->base.id);
				if (copyout(&crtc->base.id, crtc_id +
				    copied, sizeof(uint32_t))) {
					ret = EFAULT;
					goto out;
				}
				copied++;
			}
		} else {
			for (i = 0; i < mode_group->num_crtcs; i++) {
				if (copyout(&mode_group->id_list[i],
				    crtc_id + copied, sizeof(uint32_t))) {
					ret = EFAULT;
					goto out;
				}
				copied++;
			}
		}
	}
	card_res->count_crtcs = crtc_count;

	/* Encoders */
	if (card_res->count_encoders >= encoder_count) {
		copied = 0;
		encoder_id = (uint32_t *)(uintptr_t)card_res->encoder_id_ptr;
#if 1
		if (file_priv->master) {
#else
		if (file_priv->masterp->minor->type == DRM_MINOR_CONTROL) {
#endif
			list_for_each_entry(encoder,
					    &dev->mode_config.encoder_list,
					    head) {
				DRM_DEBUG_KMS("[ENCODER:%d:%s]\n", encoder->base.id,
						drm_get_encoder_name(encoder));
				if (copyout(&encoder->base.id, encoder_id +
				    copied, sizeof(uint32_t))) {
					ret = EFAULT;
					goto out;
				}
				copied++;
			}
		} else {
			for (i = mode_group->num_crtcs;
			    i < mode_group->num_crtcs + mode_group->num_encoders;
			     i++) {
				if (copyout(&mode_group->id_list[i],
				    encoder_id + copied, sizeof(uint32_t))) {
					ret = EFAULT;
					goto out;
				}
				copied++;
			}

		}
	}
	card_res->count_encoders = encoder_count;

	/* Connectors */
	if (card_res->count_connectors >= connector_count) {
		copied = 0;
		connector_id = (uint32_t *)(uintptr_t)card_res->connector_id_ptr;
#if 1
		if (file_priv->master) {
#else
		if (file_priv->masterp->minor->type == DRM_MINOR_CONTROL) {
#endif
			list_for_each_entry(connector,
					    &dev->mode_config.connector_list,
					    head) {
				DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
					connector->base.id,
					drm_get_connector_name(connector));
				if (copyout(&connector->base.id,
				    connector_id + copied, sizeof(uint32_t))) {
					ret = EFAULT;
					goto out;
				}
				copied++;
			}
		} else {
			int start = mode_group->num_crtcs +
				mode_group->num_encoders;
			for (i = start; i < start + mode_group->num_connectors; i++) {
				if (copyout(&mode_group->id_list[i],
				    connector_id + copied, sizeof(uint32_t))) {
					ret = EFAULT;
					goto out;
				}
				copied++;
			}
		}
	}
	card_res->count_connectors = connector_count;

	DRM_DEBUG_KMS("CRTC[%d] CONNECTORS[%d] ENCODERS[%d]\n", card_res->count_crtcs,
		  card_res->count_connectors, card_res->count_encoders);

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

/**
 * drm_mode_getcrtc - get CRTC configuration
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Construct a CRTC configuration structure to return to the user.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_getcrtc(struct drm_device *dev,
		     void *data, struct drm_file *file_priv)
{
	struct drm_mode_crtc *crtc_resp = data;
	struct drm_crtc *crtc;
	struct drm_mode_object *obj;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return (EINVAL);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);

	obj = drm_mode_object_find(dev, crtc_resp->crtc_id,
				   DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		ret = (EINVAL);
		goto out;
	}
	crtc = obj_to_crtc(obj);

	crtc_resp->x = crtc->x;
	crtc_resp->y = crtc->y;
	crtc_resp->gamma_size = crtc->gamma_size;
	if (crtc->fb)
		crtc_resp->fb_id = crtc->fb->base.id;
	else
		crtc_resp->fb_id = 0;

	if (crtc->enabled) {

		drm_crtc_convert_to_umode(&crtc_resp->mode, &crtc->mode);
		crtc_resp->mode_valid = 1;

	} else {
		crtc_resp->mode_valid = 0;
	}

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

/**
 * drm_mode_getconnector - get connector configuration
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Construct a connector configuration structure to return to the user.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_getconnector(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct drm_mode_get_connector *out_resp = data;
	struct drm_mode_object *obj;
	struct drm_connector *connector;
	struct drm_display_mode *mode;
	int mode_count = 0;
	int props_count = 0;
	int encoders_count = 0;
	int ret = 0;
	int copied = 0;
	int i;
	struct drm_mode_modeinfo u_mode;
	struct drm_mode_modeinfo __user *mode_ptr;
	uint32_t *prop_ptr;
	uint64_t *prop_values;
	uint32_t *encoder_ptr;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return (EINVAL);

	memset(&u_mode, 0, sizeof(struct drm_mode_modeinfo));

	DRM_DEBUG_KMS("[CONNECTOR:%d:?]\n", out_resp->connector_id);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);

	obj = drm_mode_object_find(dev, out_resp->connector_id,
				   DRM_MODE_OBJECT_CONNECTOR);
	if (!obj) {
		ret = EINVAL;
		goto out;
	}
	connector = obj_to_connector(obj);

	for (i = 0; i < DRM_CONNECTOR_MAX_PROPERTY; i++) {
		if (connector->property_ids[i] != 0) {
			props_count++;
		}
	}

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		if (connector->encoder_ids[i] != 0) {
			encoders_count++;
		}
	}

	if (out_resp->count_modes == 0) {
		connector->funcs->fill_modes(connector,
					     dev->mode_config.max_width,
					     dev->mode_config.max_height);
	}

	/* delayed so we get modes regardless of pre-fill_modes state */
	list_for_each_entry(mode, &connector->modes, head)
		mode_count++;

	out_resp->connector_id = connector->base.id;
	out_resp->connector_type = connector->connector_type;
	out_resp->connector_type_id = connector->connector_type_id;
	out_resp->mm_width = connector->display_info.width_mm;
	out_resp->mm_height = connector->display_info.height_mm;
	out_resp->subpixel = connector->display_info.subpixel_order;
	out_resp->connection = connector->status;
	if (connector->encoder)
		out_resp->encoder_id = connector->encoder->base.id;
	else
		out_resp->encoder_id = 0;

	/*
	 * This ioctl is called twice, once to determine how much space is
	 * needed, and the 2nd time to fill it.
	 */
	if ((out_resp->count_modes >= mode_count) && mode_count) {
		copied = 0;
		mode_ptr = (struct drm_mode_modeinfo *)(uintptr_t)out_resp->modes_ptr;
		list_for_each_entry(mode, &connector->modes, head) {
			drm_crtc_convert_to_umode(&u_mode, mode);
			if (copyout(&u_mode, mode_ptr + copied,
			    sizeof(u_mode))) {
				ret = EFAULT;
				goto out;
			}
			copied++;
		}
	}
	out_resp->count_modes = mode_count;

	if ((out_resp->count_props >= props_count) && props_count) {
		copied = 0;
		prop_ptr = (uint32_t *)(uintptr_t)(out_resp->props_ptr);
		prop_values = (uint64_t *)(uintptr_t)(out_resp->prop_values_ptr);
		for (i = 0; i < DRM_CONNECTOR_MAX_PROPERTY; i++) {
			if (connector->property_ids[i] != 0) {
				if (copyout(&connector->property_ids[i],
				    prop_ptr + copied, sizeof(uint32_t))) {
					ret = EFAULT;
					goto out;
				}

				if (copyout(&connector->property_values[i],
				    prop_values + copied, sizeof(uint64_t))) {
					ret = EFAULT;
					goto out;
				}
				copied++;
			}
		}
	}
	out_resp->count_props = props_count;

	if ((out_resp->count_encoders >= encoders_count) && encoders_count) {
		copied = 0;
		encoder_ptr = (uint32_t *)(uintptr_t)(out_resp->encoders_ptr);
		for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
			if (connector->encoder_ids[i] != 0) {
				if (copyout(&connector->encoder_ids[i],
				    encoder_ptr + copied, sizeof(uint32_t))) {
					ret = EFAULT;
					goto out;
				}
				copied++;
			}
		}
	}
	out_resp->count_encoders = encoders_count;

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

int drm_mode_getencoder(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_mode_get_encoder *enc_resp = data;
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return (EINVAL);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	obj = drm_mode_object_find(dev, enc_resp->encoder_id,
				   DRM_MODE_OBJECT_ENCODER);
	if (!obj) {
		ret = EINVAL;
		goto out;
	}
	encoder = obj_to_encoder(obj);

	if (encoder->crtc)
		enc_resp->crtc_id = encoder->crtc->base.id;
	else
		enc_resp->crtc_id = 0;
	enc_resp->encoder_type = encoder->encoder_type;
	enc_resp->encoder_id = encoder->base.id;
	enc_resp->possible_crtcs = encoder->possible_crtcs;
	enc_resp->possible_clones = encoder->possible_clones;

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

/**
 * drm_mode_getplane_res - get plane info
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Return an plane count and set of IDs.
 */
int drm_mode_getplane_res(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	struct drm_mode_get_plane_res *plane_resp = data;
	struct drm_mode_config *config;
	struct drm_plane *plane;
	uint32_t *plane_ptr;
	int copied = 0, ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return (EINVAL);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	config = &dev->mode_config;

	/*
	 * This ioctl is called twice, once to determine how much space is
	 * needed, and the 2nd time to fill it.
	 */
	if (config->num_plane &&
	    (plane_resp->count_planes >= config->num_plane)) {
		plane_ptr = (uint32_t *)(unsigned long)plane_resp->plane_id_ptr;

		list_for_each_entry(plane, &config->plane_list, head) {
			if (copyout(&plane->base.id, plane_ptr + copied,
			    sizeof(uint32_t))) {
				ret = EFAULT;
				goto out;
			}
			copied++;
		}
	}
	plane_resp->count_planes = config->num_plane;

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

/**
 * drm_mode_getplane - get plane info
 * @dev: DRM device
 * @data: ioctl data
 * @file_priv: DRM file info
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Return plane info, including formats supported, gamma size, any
 * current fb, etc.
 */
int drm_mode_getplane(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_mode_get_plane *plane_resp = data;
	struct drm_mode_object *obj;
	struct drm_plane *plane;
	uint32_t *format_ptr;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return (EINVAL);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	obj = drm_mode_object_find(dev, plane_resp->plane_id,
				   DRM_MODE_OBJECT_PLANE);
	if (!obj) {
		ret = ENOENT;
		goto out;
	}
	plane = obj_to_plane(obj);

	if (plane->crtc)
		plane_resp->crtc_id = plane->crtc->base.id;
	else
		plane_resp->crtc_id = 0;

	if (plane->fb)
		plane_resp->fb_id = plane->fb->base.id;
	else
		plane_resp->fb_id = 0;

	plane_resp->plane_id = plane->base.id;
	plane_resp->possible_crtcs = plane->possible_crtcs;
	plane_resp->gamma_size = plane->gamma_size;

	/*
	 * This ioctl is called twice, once to determine how much space is
	 * needed, and the 2nd time to fill it.
	 */
	if (plane->format_count &&
	    (plane_resp->count_format_types >= plane->format_count)) {
		format_ptr = (uint32_t *)(unsigned long)plane_resp->format_type_ptr;
		if (copyout(format_ptr,
				 plane->format_types,
				 sizeof(uint32_t) * plane->format_count)) {
			ret = EFAULT;
			goto out;
		}
	}
	plane_resp->count_format_types = plane->format_count;

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

/**
 * drm_mode_setplane - set up or tear down an plane
 * @dev: DRM device
 * @data: ioctl data*
 * @file_prive: DRM file info
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Set plane info, including placement, fb, scaling, and other factors.
 * Or pass a NULL fb to disable.
 */
int drm_mode_setplane(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_mode_set_plane *plane_req = data;
	struct drm_mode_object *obj;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	struct drm_framebuffer *fb;
	int ret = 0;
	unsigned int fb_width, fb_height;
	int i;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return (EINVAL);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);

	/*
	 * First, find the plane, crtc, and fb objects.  If not available,
	 * we don't bother to call the driver.
	 */
	obj = drm_mode_object_find(dev, plane_req->plane_id,
				   DRM_MODE_OBJECT_PLANE);
	if (!obj) {
		DRM_DEBUG_KMS("Unknown plane ID %d\n",
			      plane_req->plane_id);
		ret = ENOENT;
		goto out;
	}
	plane = obj_to_plane(obj);

	/* No fb means shut it down */
	if (!plane_req->fb_id) {
		plane->funcs->disable_plane(plane);
		plane->crtc = NULL;
		plane->fb = NULL;
		goto out;
	}

	obj = drm_mode_object_find(dev, plane_req->crtc_id,
				   DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		DRM_DEBUG_KMS("Unknown crtc ID %d\n",
			      plane_req->crtc_id);
		ret = ENOENT;
		goto out;
	}
	crtc = obj_to_crtc(obj);

	obj = drm_mode_object_find(dev, plane_req->fb_id,
				   DRM_MODE_OBJECT_FB);
	if (!obj) {
		DRM_DEBUG_KMS("Unknown framebuffer ID %d\n",
			      plane_req->fb_id);
		ret = ENOENT;
		goto out;
	}
	fb = obj_to_fb(obj);

	/* Check whether this plane supports the fb pixel format. */
	for (i = 0; i < plane->format_count; i++)
		if (fb->pixel_format == plane->format_types[i])
			break;
	if (i == plane->format_count) {
		DRM_DEBUG_KMS("Invalid pixel format 0x%08x\n", fb->pixel_format);
		ret = EINVAL;
		goto out;
	}

	fb_width = fb->width << 16;
	fb_height = fb->height << 16;

	/* Make sure source coordinates are inside the fb. */
	if (plane_req->src_w > fb_width ||
	    plane_req->src_x > fb_width - plane_req->src_w ||
	    plane_req->src_h > fb_height ||
	    plane_req->src_y > fb_height - plane_req->src_h) {
		DRM_DEBUG_KMS("Invalid source coordinates "
			      "%u.%06ux%u.%06u+%u.%06u+%u.%06u\n",
			      plane_req->src_w >> 16,
			      ((plane_req->src_w & 0xffff) * 15625) >> 10,
			      plane_req->src_h >> 16,
			      ((plane_req->src_h & 0xffff) * 15625) >> 10,
			      plane_req->src_x >> 16,
			      ((plane_req->src_x & 0xffff) * 15625) >> 10,
			      plane_req->src_y >> 16,
			      ((plane_req->src_y & 0xffff) * 15625) >> 10);
		ret = ENOSPC;
		goto out;
	}

	/* Give drivers some help against integer overflows */
	if (plane_req->crtc_w > INT_MAX ||
	    plane_req->crtc_x > INT_MAX - (int32_t) plane_req->crtc_w ||
	    plane_req->crtc_h > INT_MAX ||
	    plane_req->crtc_y > INT_MAX - (int32_t) plane_req->crtc_h) {
		DRM_DEBUG_KMS("Invalid CRTC coordinates %ux%u+%d+%d\n",
			      plane_req->crtc_w, plane_req->crtc_h,
			      plane_req->crtc_x, plane_req->crtc_y);
		ret = ERANGE;
		goto out;
	}

	ret = -plane->funcs->update_plane(plane, crtc, fb,
					 plane_req->crtc_x, plane_req->crtc_y,
					 plane_req->crtc_w, plane_req->crtc_h,
					 plane_req->src_x, plane_req->src_y,
					 plane_req->src_w, plane_req->src_h);
	if (!ret) {
		plane->crtc = crtc;
		plane->fb = fb;
	}

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);

	return ret;
}

/**
 * drm_mode_setcrtc - set CRTC configuration
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Build a new CRTC configuration based on user request.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_setcrtc(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_mode_crtc *crtc_req = data;
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	struct drm_connector **connector_set = NULL, *connector;
	struct drm_framebuffer *fb = NULL;
	struct drm_display_mode *mode = NULL;
	struct drm_mode_set set;
	uint32_t *set_connectors_ptr;
	int ret = 0;
	int i;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return (EINVAL);

	/* For some reason crtc x/y offsets are signed internally. */
	if (crtc_req->x > INT_MAX || crtc_req->y > INT_MAX)
		return (ERANGE);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	obj = drm_mode_object_find(dev, crtc_req->crtc_id,
				   DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		DRM_DEBUG_KMS("Unknown CRTC ID %d\n", crtc_req->crtc_id);
		ret = EINVAL;
		goto out;
	}
	crtc = obj_to_crtc(obj);
	DRM_DEBUG_KMS("[CRTC:%d]\n", crtc->base.id);

	if (crtc_req->mode_valid) {
		/* If we have a mode we need a framebuffer. */
		/* If we pass -1, set the mode with the currently bound fb */
		if (crtc_req->fb_id == -1) {
			if (!crtc->fb) {
				DRM_DEBUG_KMS("CRTC doesn't have current FB\n");
				ret = -EINVAL;
				goto out;
			}
			fb = crtc->fb;
		} else {
			obj = drm_mode_object_find(dev, crtc_req->fb_id,
						   DRM_MODE_OBJECT_FB);
			if (!obj) {
				DRM_DEBUG_KMS("Unknown FB ID%d\n",
						crtc_req->fb_id);
				ret = EINVAL;
				goto out;
			}
			fb = obj_to_fb(obj);
		}

		mode = drm_mode_create(dev);
		if (!mode) {
			ret = ENOMEM;
			goto out;
		}

		ret = drm_crtc_convert_umode(mode, &crtc_req->mode);
		if (ret) {
			DRM_DEBUG_KMS("Invalid mode\n");
			goto out;
		}

		drm_mode_set_crtcinfo(mode, CRTC_INTERLACE_HALVE_V);

		if (mode->hdisplay > fb->width ||
		    mode->vdisplay > fb->height ||
		    crtc_req->x > fb->width - mode->hdisplay ||
		    crtc_req->y > fb->height - mode->vdisplay) {
			DRM_DEBUG_KMS("Invalid CRTC viewport %ux%u+%u+%u for fb size %ux%u.\n",
				      mode->hdisplay, mode->vdisplay,
				      crtc_req->x, crtc_req->y,
				      fb->width, fb->height);
			ret = ENOSPC;
			goto out;
		}
	}

	if (crtc_req->count_connectors == 0 && mode) {
		DRM_DEBUG_KMS("Count connectors is 0 but mode set\n");
		ret = EINVAL;
		goto out;
	}

	if (crtc_req->count_connectors > 0 && (!mode || !fb)) {
		DRM_DEBUG_KMS("Count connectors is %d but no mode or fb set\n",
			  crtc_req->count_connectors);
		ret = EINVAL;
		goto out;
	}

	if (crtc_req->count_connectors > 0) {
		u32 out_id;

		/* Avoid unbounded kernel memory allocation */
		if (crtc_req->count_connectors > config->num_connector) {
			ret = EINVAL;
			goto out;
		}

		connector_set = kmalloc(crtc_req->count_connectors *
		    sizeof(struct drm_connector *), DRM_MEM_KMS, M_WAITOK);

		for (i = 0; i < crtc_req->count_connectors; i++) {
			set_connectors_ptr = (uint32_t *)(uintptr_t)crtc_req->set_connectors_ptr;
			if (copyin(&set_connectors_ptr[i], &out_id, sizeof(uint32_t))) {
				ret = EFAULT;
				goto out;
			}

			obj = drm_mode_object_find(dev, out_id,
						   DRM_MODE_OBJECT_CONNECTOR);
			if (!obj) {
				DRM_DEBUG_KMS("Connector id %d unknown\n",
						out_id);
				ret = EINVAL;
				goto out;
			}
			connector = obj_to_connector(obj);
			DRM_DEBUG_KMS("[CONNECTOR:%d:%s]\n",
					connector->base.id,
					drm_get_connector_name(connector));

			connector_set[i] = connector;
		}
	}

	set.crtc = crtc;
	set.x = crtc_req->x;
	set.y = crtc_req->y;
	set.mode = mode;
	set.connectors = connector_set;
	set.num_connectors = crtc_req->count_connectors;
	set.fb = fb;
	ret = crtc->funcs->set_config(&set);

out:
	drm_free(connector_set, DRM_MEM_KMS);
	drm_mode_destroy(dev, mode);
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

int drm_mode_cursor_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file_priv)
{
	struct drm_mode_cursor *req = data;
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return (EINVAL);

	if (!req->flags)
		return (EINVAL);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	obj = drm_mode_object_find(dev, req->crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		DRM_DEBUG_KMS("Unknown CRTC ID %d\n", req->crtc_id);
		ret = EINVAL;
		goto out;
	}
	crtc = obj_to_crtc(obj);

	if (req->flags & DRM_MODE_CURSOR_BO) {
		if (!crtc->funcs->cursor_set) {
			ret = ENXIO;
			goto out;
		}
		/* Turns off the cursor if handle is 0 */
		ret = -crtc->funcs->cursor_set(crtc, file_priv, req->handle,
					      req->width, req->height);
	}

	if (req->flags & DRM_MODE_CURSOR_MOVE) {
		if (crtc->funcs->cursor_move) {
			ret = crtc->funcs->cursor_move(crtc, req->x, req->y);
		} else {
			ret = EFAULT;
			goto out;
		}
	}
out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

/* Original addfb only supported RGB formats, so figure out which one */
uint32_t drm_mode_legacy_fb_format(uint32_t bpp, uint32_t depth)
{
	uint32_t fmt;

	switch (bpp) {
	case 8:
		fmt = DRM_FORMAT_RGB332;
		break;
	case 16:
		if (depth == 15)
			fmt = DRM_FORMAT_XRGB1555;
		else
			fmt = DRM_FORMAT_RGB565;
		break;
	case 24:
		fmt = DRM_FORMAT_RGB888;
		break;
	case 32:
		if (depth == 24)
			fmt = DRM_FORMAT_XRGB8888;
		else if (depth == 30)
			fmt = DRM_FORMAT_XRGB2101010;
		else
			fmt = DRM_FORMAT_ARGB8888;
		break;
	default:
		DRM_ERROR("bad bpp, assuming RGB24 pixel format\n");
		fmt = DRM_FORMAT_XRGB8888;
		break;
	}

	return fmt;
}

/**
 * drm_mode_addfb - add an FB to the graphics configuration
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Add a new FB to the specified CRTC, given a user request.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_addfb(struct drm_device *dev,
		   void *data, struct drm_file *file_priv)
{
	struct drm_mode_fb_cmd *or = data;
	struct drm_mode_fb_cmd2 r = {};
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_framebuffer *fb;
	int ret = 0;

	/* Use new struct with format internally */
	r.fb_id = or->fb_id;
	r.width = or->width;
	r.height = or->height;
	r.pitches[0] = or->pitch;
	r.pixel_format = drm_mode_legacy_fb_format(or->bpp, or->depth);
	r.handles[0] = or->handle;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return (EINVAL);

	if ((config->min_width > r.width) || (r.width > config->max_width))
		return (EINVAL);
	if ((config->min_height > r.height) || (r.height > config->max_height))
		return (EINVAL);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);

	ret = -dev->mode_config.funcs->fb_create(dev, file_priv, &r, &fb);
	if (ret != 0) {
		DRM_ERROR("could not create framebuffer, error %d\n", ret);
		goto out;
	}

	or->fb_id = fb->base.id;
	list_add(&fb->filp_head, &file_priv->fbs);
	DRM_DEBUG_KMS("[FB:%d]\n", fb->base.id);

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

static int format_check(struct drm_mode_fb_cmd2 *r)
{
	uint32_t format = r->pixel_format & ~DRM_FORMAT_BIG_ENDIAN;

	switch (format) {
	case DRM_FORMAT_C8:
	case DRM_FORMAT_RGB332:
	case DRM_FORMAT_BGR233:
	case DRM_FORMAT_XRGB4444:
	case DRM_FORMAT_XBGR4444:
	case DRM_FORMAT_RGBX4444:
	case DRM_FORMAT_BGRX4444:
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_ABGR4444:
	case DRM_FORMAT_RGBA4444:
	case DRM_FORMAT_BGRA4444:
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_RGBX5551:
	case DRM_FORMAT_BGRX5551:
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_BGRA5551:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_RGBX1010102:
	case DRM_FORMAT_BGRX1010102:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_BGRA1010102:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_AYUV:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_YUV410:
	case DRM_FORMAT_YVU410:
	case DRM_FORMAT_YUV411:
	case DRM_FORMAT_YVU411:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YUV444:
	case DRM_FORMAT_YVU444:
		return 0;
	default:
		return (EINVAL);
	}
}

/**
 * drm_mode_addfb2 - add an FB to the graphics configuration
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Add a new FB to the specified CRTC, given a user request with format.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_addfb2(struct drm_device *dev,
		    void *data, struct drm_file *file_priv)
{
	struct drm_mode_fb_cmd2 *r = data;
	struct drm_mode_config *config = &dev->mode_config;
	struct drm_framebuffer *fb;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return (EINVAL);

	if ((config->min_width > r->width) || (r->width > config->max_width)) {
		DRM_ERROR("bad framebuffer width %d, should be >= %d && <= %d\n",
			  r->width, config->min_width, config->max_width);
		return (EINVAL);
	}
	if ((config->min_height > r->height) || (r->height > config->max_height)) {
		DRM_ERROR("bad framebuffer height %d, should be >= %d && <= %d\n",
			  r->height, config->min_height, config->max_height);
		return (EINVAL);
	}

	ret = format_check(r);
	if (ret) {
		DRM_ERROR("bad framebuffer format 0x%08x\n", r->pixel_format);
		return ret;
	}

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);

	/* TODO check buffer is sufficiently large */
	/* TODO setup destructor callback */

	ret = -dev->mode_config.funcs->fb_create(dev, file_priv, r, &fb);
	if (ret != 0) {
		DRM_ERROR("could not create framebuffer, error %d\n", ret);
		goto out;
	}

	r->fb_id = fb->base.id;
	list_add(&fb->filp_head, &file_priv->fbs);
	DRM_DEBUG_KMS("[FB:%d]\n", fb->base.id);

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return (ret);
}

/**
 * drm_mode_rmfb - remove an FB from the configuration
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Remove the FB specified by the user.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_rmfb(struct drm_device *dev,
		   void *data, struct drm_file *file_priv)
{
	struct drm_mode_object *obj;
	struct drm_framebuffer *fb = NULL;
	struct drm_framebuffer *fbl = NULL;
	uint32_t *id = data;
	int ret = 0;
	int found = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return (EINVAL);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	obj = drm_mode_object_find(dev, *id, DRM_MODE_OBJECT_FB);
	/* TODO check that we really get a framebuffer back. */
	if (!obj) {
		ret = EINVAL;
		goto out;
	}
	fb = obj_to_fb(obj);

	list_for_each_entry(fbl, &file_priv->fbs, filp_head)
		if (fb == fbl)
			found = 1;

	if (!found) {
		ret = EINVAL;
		goto out;
	}

	/* TODO release all crtc connected to the framebuffer */
	/* TODO unhock the destructor from the buffer object */

	list_del(&fb->filp_head);
	fb->funcs->destroy(fb);

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

/**
 * drm_mode_getfb - get FB info
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Lookup the FB given its ID and return info about it.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_getfb(struct drm_device *dev,
		   void *data, struct drm_file *file_priv)
{
	struct drm_mode_fb_cmd *r = data;
	struct drm_mode_object *obj;
	struct drm_framebuffer *fb;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return (EINVAL);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	obj = drm_mode_object_find(dev, r->fb_id, DRM_MODE_OBJECT_FB);
	if (!obj) {
		ret = EINVAL;
		goto out;
	}
	fb = obj_to_fb(obj);

	r->height = fb->height;
	r->width = fb->width;
	r->depth = fb->depth;
	r->bpp = fb->bits_per_pixel;
	r->pitch = fb->pitches[0];
	fb->funcs->create_handle(fb, file_priv, &r->handle);

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

int drm_mode_dirtyfb_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv)
{
	struct drm_clip_rect __user *clips_ptr;
	struct drm_clip_rect *clips = NULL;
	struct drm_mode_fb_dirty_cmd *r = data;
	struct drm_mode_object *obj;
	struct drm_framebuffer *fb;
	unsigned flags;
	int num_clips;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return (EINVAL);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	obj = drm_mode_object_find(dev, r->fb_id, DRM_MODE_OBJECT_FB);
	if (!obj) {
		ret = EINVAL;
		goto out_err1;
	}
	fb = obj_to_fb(obj);

	num_clips = r->num_clips;
	clips_ptr = (struct drm_clip_rect *)(unsigned long)r->clips_ptr;

	if (!num_clips != !clips_ptr) {
		ret = EINVAL;
		goto out_err1;
	}

	flags = DRM_MODE_FB_DIRTY_FLAGS & r->flags;

	/* If userspace annotates copy, clips must come in pairs */
	if (flags & DRM_MODE_FB_DIRTY_ANNOTATE_COPY && (num_clips % 2)) {
		ret = EINVAL;
		goto out_err1;
	}

	if (num_clips && clips_ptr) {
		if (num_clips < 0 || num_clips > DRM_MODE_FB_DIRTY_MAX_CLIPS) {
			ret = EINVAL;
			goto out_err1;
		}
		clips = kmalloc(num_clips * sizeof(*clips), DRM_MEM_KMS,
		    M_WAITOK | M_ZERO);

		ret = copyin(clips_ptr, clips, num_clips * sizeof(*clips));
		if (ret)
			goto out_err2;
	}

	if (fb->funcs->dirty) {
		ret = -fb->funcs->dirty(fb, file_priv, flags, r->color,
				       clips, num_clips);
	} else {
		ret = ENOSYS;
		goto out_err2;
	}

out_err2:
	drm_free(clips, DRM_MEM_KMS);
out_err1:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}


/**
 * drm_fb_release - remove and free the FBs on this file
 * @filp: file * from the ioctl
 *
 * LOCKING:
 * Takes mode config lock.
 *
 * Destroy all the FBs associated with @filp.
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
void drm_fb_release(struct drm_file *priv)
{
#if 1
	struct drm_device *dev = priv->dev;
#else
	struct drm_device *dev = priv->minor->dev;
#endif
	struct drm_framebuffer *fb, *tfb;

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	list_for_each_entry_safe(fb, tfb, &priv->fbs, filp_head) {
		list_del(&fb->filp_head);
		fb->funcs->destroy(fb);
	}
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
}

/**
 * drm_mode_attachmode - add a mode to the user mode list
 * @dev: DRM device
 * @connector: connector to add the mode to
 * @mode: mode to add
 *
 * Add @mode to @connector's user mode list.
 */
static void drm_mode_attachmode(struct drm_device *dev,
				struct drm_connector *connector,
				struct drm_display_mode *mode)
{
	list_add_tail(&mode->head, &connector->user_modes);
}

int drm_mode_attachmode_crtc(struct drm_device *dev, struct drm_crtc *crtc,
			     const struct drm_display_mode *mode)
{
	struct drm_connector *connector;
	int ret = 0;
	struct drm_display_mode *dup_mode, *next;
	LINUX_LIST_HEAD(list);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (!connector->encoder)
			continue;
		if (connector->encoder->crtc == crtc) {
			dup_mode = drm_mode_duplicate(dev, mode);
			if (!dup_mode) {
				ret = ENOMEM;
				goto out;
			}
			list_add_tail(&dup_mode->head, &list);
		}
	}

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (!connector->encoder)
			continue;
		if (connector->encoder->crtc == crtc)
			list_move_tail(list.next, &connector->user_modes);
	}

	KASSERT(!list_empty(&list), ("list empty"));

 out:
	list_for_each_entry_safe(dup_mode, next, &list, head)
		drm_mode_destroy(dev, dup_mode);

	return ret;
}

static int drm_mode_detachmode(struct drm_device *dev,
			       struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	int found = 0;
	int ret = 0;
	struct drm_display_mode *match_mode, *t;

	list_for_each_entry_safe(match_mode, t, &connector->user_modes, head) {
		if (drm_mode_equal(match_mode, mode)) {
			list_del(&match_mode->head);
			drm_mode_destroy(dev, match_mode);
			found = 1;
			break;
		}
	}

	if (!found)
		ret = -EINVAL;

	return ret;
}

int drm_mode_detachmode_crtc(struct drm_device *dev, struct drm_display_mode *mode)
{
	struct drm_connector *connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		drm_mode_detachmode(dev, connector, mode);
	}
	return 0;
}

/**
 * drm_fb_attachmode - Attach a user mode to an connector
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * This attaches a user specified mode to an connector.
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_attachmode_ioctl(struct drm_device *dev,
			      void *data, struct drm_file *file_priv)
{
	struct drm_mode_mode_cmd *mode_cmd = data;
	struct drm_connector *connector;
	struct drm_display_mode *mode;
	struct drm_mode_object *obj;
	struct drm_mode_modeinfo *umode = &mode_cmd->mode;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);

	obj = drm_mode_object_find(dev, mode_cmd->connector_id, DRM_MODE_OBJECT_CONNECTOR);
	if (!obj) {
		ret = -EINVAL;
		goto out;
	}
	connector = obj_to_connector(obj);

	mode = drm_mode_create(dev);
	if (!mode) {
		ret = -ENOMEM;
		goto out;
	}

	ret = drm_crtc_convert_umode(mode, umode);
	if (ret) {
		DRM_DEBUG_KMS("Invalid mode\n");
		drm_mode_destroy(dev, mode);
		goto out;
	}

	drm_mode_attachmode(dev, connector, mode);
out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}


/**
 * drm_fb_detachmode - Detach a user specified mode from an connector
 * @inode: inode from the ioctl
 * @filp: file * from the ioctl
 * @cmd: cmd from ioctl
 * @arg: arg from ioctl
 *
 * Called by the user via ioctl.
 *
 * RETURNS:
 * Zero on success, errno on failure.
 */
int drm_mode_detachmode_ioctl(struct drm_device *dev,
			      void *data, struct drm_file *file_priv)
{
	struct drm_mode_object *obj;
	struct drm_mode_mode_cmd *mode_cmd = data;
	struct drm_connector *connector;
	struct drm_display_mode mode;
	struct drm_mode_modeinfo *umode = &mode_cmd->mode;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);

	obj = drm_mode_object_find(dev, mode_cmd->connector_id, DRM_MODE_OBJECT_CONNECTOR);
	if (!obj) {
		ret = -EINVAL;
		goto out;
	}
	connector = obj_to_connector(obj);

	ret = drm_crtc_convert_umode(&mode, umode);
	if (ret) {
		DRM_DEBUG_KMS("Invalid mode\n");
		goto out;
	}

	ret = drm_mode_detachmode(dev, connector, &mode);
out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

struct drm_property *drm_property_create(struct drm_device *dev, int flags,
					 const char *name, int num_values)
{
	struct drm_property *property = NULL;
	int ret;

	property = kmalloc(sizeof(struct drm_property), DRM_MEM_KMS,
	    M_WAITOK | M_ZERO);

	if (num_values) {
		property->values = kmalloc(sizeof(uint64_t)*num_values, DRM_MEM_KMS,
		    M_WAITOK | M_ZERO);
	}

	ret = drm_mode_object_get(dev, &property->base, DRM_MODE_OBJECT_PROPERTY);
	if (ret)
		goto fail;
	property->flags = flags;
	property->num_values = num_values;
	INIT_LIST_HEAD(&property->enum_blob_list);

	if (name) {
		strncpy(property->name, name, DRM_PROP_NAME_LEN);
		property->name[DRM_PROP_NAME_LEN-1] = '\0';
	}

	list_add_tail(&property->head, &dev->mode_config.property_list);
	return property;

fail:
	drm_free(property->values, DRM_MEM_KMS);
	drm_free(property, DRM_MEM_KMS);
	return (NULL);
}

struct drm_property *drm_property_create_enum(struct drm_device *dev, int flags,
					 const char *name,
					 const struct drm_prop_enum_list *props,
					 int num_values)
{
	struct drm_property *property;
	int i, ret;

	flags |= DRM_MODE_PROP_ENUM;

	property = drm_property_create(dev, flags, name, num_values);
	if (!property)
		return NULL;

	for (i = 0; i < num_values; i++) {
		ret = drm_property_add_enum(property, i,
				      props[i].type,
				      props[i].name);
		if (ret) {
			drm_property_destroy(dev, property);
			return NULL;
		}
	}

	return property;
}

struct drm_property *drm_property_create_range(struct drm_device *dev, int flags,
					 const char *name,
					 uint64_t min, uint64_t max)
{
	struct drm_property *property;

	flags |= DRM_MODE_PROP_RANGE;

	property = drm_property_create(dev, flags, name, 2);
	if (!property)
		return NULL;

	property->values[0] = min;
	property->values[1] = max;

	return property;
}

int drm_property_add_enum(struct drm_property *property, int index,
			  uint64_t value, const char *name)
{
	struct drm_property_enum *prop_enum;

	if (!(property->flags & DRM_MODE_PROP_ENUM))
		return -EINVAL;

	if (!list_empty(&property->enum_blob_list)) {
		list_for_each_entry(prop_enum, &property->enum_blob_list, head) {
			if (prop_enum->value == value) {
				strncpy(prop_enum->name, name, DRM_PROP_NAME_LEN);
				prop_enum->name[DRM_PROP_NAME_LEN-1] = '\0';
				return 0;
			}
		}
	}

	prop_enum = kmalloc(sizeof(struct drm_property_enum), DRM_MEM_KMS,
	    M_WAITOK | M_ZERO);

	strncpy(prop_enum->name, name, DRM_PROP_NAME_LEN);
	prop_enum->name[DRM_PROP_NAME_LEN-1] = '\0';
	prop_enum->value = value;

	property->values[index] = value;
	list_add_tail(&prop_enum->head, &property->enum_blob_list);
	return 0;
}

void drm_property_destroy(struct drm_device *dev, struct drm_property *property)
{
	struct drm_property_enum *prop_enum, *pt;

	list_for_each_entry_safe(prop_enum, pt, &property->enum_blob_list, head) {
		list_del(&prop_enum->head);
		drm_free(prop_enum, DRM_MEM_KMS);
	}

	if (property->num_values)
		drm_free(property->values, DRM_MEM_KMS);
	drm_mode_object_put(dev, &property->base);
	list_del(&property->head);
	drm_free(property, DRM_MEM_KMS);
}

int drm_connector_attach_property(struct drm_connector *connector,
			       struct drm_property *property, uint64_t init_val)
{
	int i;

	for (i = 0; i < DRM_CONNECTOR_MAX_PROPERTY; i++) {
		if (connector->property_ids[i] == 0) {
			connector->property_ids[i] = property->base.id;
			connector->property_values[i] = init_val;
			break;
		}
	}

	if (i == DRM_CONNECTOR_MAX_PROPERTY)
		return -EINVAL;
	return 0;
}

int drm_connector_property_set_value(struct drm_connector *connector,
				  struct drm_property *property, uint64_t value)
{
	int i;

	for (i = 0; i < DRM_CONNECTOR_MAX_PROPERTY; i++) {
		if (connector->property_ids[i] == property->base.id) {
			connector->property_values[i] = value;
			break;
		}
	}

	if (i == DRM_CONNECTOR_MAX_PROPERTY)
		return -EINVAL;
	return 0;
}

int drm_connector_property_get_value(struct drm_connector *connector,
				  struct drm_property *property, uint64_t *val)
{
	int i;

	for (i = 0; i < DRM_CONNECTOR_MAX_PROPERTY; i++) {
		if (connector->property_ids[i] == property->base.id) {
			*val = connector->property_values[i];
			break;
		}
	}

	if (i == DRM_CONNECTOR_MAX_PROPERTY)
		return -EINVAL;
	return 0;
}

int drm_mode_getproperty_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv)
{
	struct drm_mode_object *obj;
	struct drm_mode_get_property *out_resp = data;
	struct drm_property *property;
	int enum_count = 0;
	int blob_count = 0;
	int value_count = 0;
	int ret = 0, i;
	int copied;
	struct drm_property_enum *prop_enum;
	struct drm_mode_property_enum __user *enum_ptr;
	struct drm_property_blob *prop_blob;
	uint32_t *blob_id_ptr;
	uint64_t *values_ptr;
	uint32_t *blob_length_ptr;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	obj = drm_mode_object_find(dev, out_resp->prop_id, DRM_MODE_OBJECT_PROPERTY);
	if (!obj) {
		ret = -EINVAL;
		goto done;
	}
	property = obj_to_property(obj);

	if (property->flags & DRM_MODE_PROP_ENUM) {
		list_for_each_entry(prop_enum, &property->enum_blob_list, head)
			enum_count++;
	} else if (property->flags & DRM_MODE_PROP_BLOB) {
		list_for_each_entry(prop_blob, &property->enum_blob_list, head)
			blob_count++;
	}

	value_count = property->num_values;

	strncpy(out_resp->name, property->name, DRM_PROP_NAME_LEN);
	out_resp->name[DRM_PROP_NAME_LEN-1] = 0;
	out_resp->flags = property->flags;

	if ((out_resp->count_values >= value_count) && value_count) {
		values_ptr = (uint64_t *)(uintptr_t)out_resp->values_ptr;
		for (i = 0; i < value_count; i++) {
			if (copyout(&property->values[i], values_ptr + i, sizeof(uint64_t))) {
				ret = -EFAULT;
				goto done;
			}
		}
	}
	out_resp->count_values = value_count;

	if (property->flags & DRM_MODE_PROP_ENUM) {
		if ((out_resp->count_enum_blobs >= enum_count) && enum_count) {
			copied = 0;
			enum_ptr = (struct drm_mode_property_enum *)(uintptr_t)out_resp->enum_blob_ptr;
			list_for_each_entry(prop_enum, &property->enum_blob_list, head) {

				if (copyout(&prop_enum->value, &enum_ptr[copied].value, sizeof(uint64_t))) {
					ret = -EFAULT;
					goto done;
				}

				if (copyout(&prop_enum->name,
				    &enum_ptr[copied].name,DRM_PROP_NAME_LEN)) {
					ret = -EFAULT;
					goto done;
				}
				copied++;
			}
		}
		out_resp->count_enum_blobs = enum_count;
	}

	if (property->flags & DRM_MODE_PROP_BLOB) {
		if ((out_resp->count_enum_blobs >= blob_count) && blob_count) {
			copied = 0;
			blob_id_ptr = (uint32_t *)(uintptr_t)out_resp->enum_blob_ptr;
			blob_length_ptr = (uint32_t *)(uintptr_t)out_resp->values_ptr;

			list_for_each_entry(prop_blob, &property->enum_blob_list, head) {
				if (copyout(&prop_blob->base.id,
				    blob_id_ptr + copied, sizeof(uint32_t))) {
					ret = -EFAULT;
					goto done;
				}

				if (copyout(&prop_blob->length,
				    blob_length_ptr + copied, sizeof(uint32_t))) {
					ret = -EFAULT;
					goto done;
				}

				copied++;
			}
		}
		out_resp->count_enum_blobs = blob_count;
	}
done:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

static struct drm_property_blob *drm_property_create_blob(struct drm_device *dev, int length,
							  void *data)
{
	struct drm_property_blob *blob;
	int ret;

	if (!length || !data)
		return NULL;

	blob = kmalloc(sizeof(struct drm_property_blob) + length, DRM_MEM_KMS,
	    M_WAITOK | M_ZERO);

	ret = drm_mode_object_get(dev, &blob->base, DRM_MODE_OBJECT_BLOB);
	if (ret) {
		drm_free(blob, DRM_MEM_KMS);
		return (NULL);
	}

	blob->length = length;

	memcpy(blob->data, data, length);

	list_add_tail(&blob->head, &dev->mode_config.property_blob_list);
	return blob;
}

static void drm_property_destroy_blob(struct drm_device *dev,
			       struct drm_property_blob *blob)
{
	drm_mode_object_put(dev, &blob->base);
	list_del(&blob->head);
	drm_free(blob, DRM_MEM_KMS);
}

int drm_mode_getblob_ioctl(struct drm_device *dev,
			   void *data, struct drm_file *file_priv)
{
	struct drm_mode_object *obj;
	struct drm_mode_get_blob *out_resp = data;
	struct drm_property_blob *blob;
	int ret = 0;
	void *blob_ptr;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	obj = drm_mode_object_find(dev, out_resp->blob_id, DRM_MODE_OBJECT_BLOB);
	if (!obj) {
		ret = -EINVAL;
		goto done;
	}
	blob = obj_to_blob(obj);

	if (out_resp->length == blob->length) {
		blob_ptr = (void *)(unsigned long)out_resp->data;
		if (copyout(blob->data, blob_ptr, blob->length)){
			ret = -EFAULT;
			goto done;
		}
	}
	out_resp->length = blob->length;

done:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

int drm_mode_connector_update_edid_property(struct drm_connector *connector,
					    struct edid *edid)
{
	struct drm_device *dev = connector->dev;
	int ret = 0, size;

	if (connector->edid_blob_ptr)
		drm_property_destroy_blob(dev, connector->edid_blob_ptr);

	/* Delete edid, when there is none. */
	if (!edid) {
		connector->edid_blob_ptr = NULL;
		ret = drm_connector_property_set_value(connector, dev->mode_config.edid_property, 0);
		return ret;
	}

	size = EDID_LENGTH * (1 + edid->extensions);
	connector->edid_blob_ptr = drm_property_create_blob(connector->dev,
							    size, edid);

	ret = drm_connector_property_set_value(connector,
					       dev->mode_config.edid_property,
					       connector->edid_blob_ptr->base.id);

	return ret;
}

int drm_mode_connector_property_set_ioctl(struct drm_device *dev,
				       void *data, struct drm_file *file_priv)
{
	struct drm_mode_connector_set_property *out_resp = data;
	struct drm_mode_object *obj;
	struct drm_property *property;
	struct drm_connector *connector;
	int ret = -EINVAL;
	int i;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);

	obj = drm_mode_object_find(dev, out_resp->connector_id, DRM_MODE_OBJECT_CONNECTOR);
	if (!obj) {
		goto out;
	}
	connector = obj_to_connector(obj);

	for (i = 0; i < DRM_CONNECTOR_MAX_PROPERTY; i++) {
		if (connector->property_ids[i] == out_resp->prop_id)
			break;
	}

	if (i == DRM_CONNECTOR_MAX_PROPERTY) {
		goto out;
	}

	obj = drm_mode_object_find(dev, out_resp->prop_id, DRM_MODE_OBJECT_PROPERTY);
	if (!obj) {
		goto out;
	}
	property = obj_to_property(obj);

	if (property->flags & DRM_MODE_PROP_IMMUTABLE)
		goto out;

	if (property->flags & DRM_MODE_PROP_RANGE) {
		if (out_resp->value < property->values[0])
			goto out;

		if (out_resp->value > property->values[1])
			goto out;
	} else {
		int found = 0;
		for (i = 0; i < property->num_values; i++) {
			if (property->values[i] == out_resp->value) {
				found = 1;
				break;
			}
		}
		if (!found) {
			goto out;
		}
	}

	/* Do DPMS ourselves */
	if (property == connector->dev->mode_config.dpms_property) {
		if (connector->funcs->dpms)
			(*connector->funcs->dpms)(connector, (int) out_resp->value);
		ret = 0;
	} else if (connector->funcs->set_property)
		ret = connector->funcs->set_property(connector, property, out_resp->value);

	/* store the property value if successful */
	if (!ret)
		drm_connector_property_set_value(connector, property, out_resp->value);
out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

int drm_mode_connector_attach_encoder(struct drm_connector *connector,
				      struct drm_encoder *encoder)
{
	int i;

	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		if (connector->encoder_ids[i] == 0) {
			connector->encoder_ids[i] = encoder->base.id;
			return 0;
		}
	}
	return -ENOMEM;
}

void drm_mode_connector_detach_encoder(struct drm_connector *connector,
				    struct drm_encoder *encoder)
{
	int i;
	for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
		if (connector->encoder_ids[i] == encoder->base.id) {
			connector->encoder_ids[i] = 0;
			if (connector->encoder == encoder)
				connector->encoder = NULL;
			break;
		}
	}
}

int drm_mode_crtc_set_gamma_size(struct drm_crtc *crtc,
				  int gamma_size)
{
	crtc->gamma_size = gamma_size;

	crtc->gamma_store = kmalloc(gamma_size * sizeof(uint16_t) * 3,
	    DRM_MEM_KMS, M_WAITOK | M_ZERO);

	return 0;
}

int drm_mode_gamma_set_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv)
{
	struct drm_mode_crtc_lut *crtc_lut = data;
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	void *r_base, *g_base, *b_base;
	int size;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	obj = drm_mode_object_find(dev, crtc_lut->crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		ret = -EINVAL;
		goto out;
	}
	crtc = obj_to_crtc(obj);

	/* memcpy into gamma store */
	if (crtc_lut->gamma_size != crtc->gamma_size) {
		ret = -EINVAL;
		goto out;
	}

	size = crtc_lut->gamma_size * (sizeof(uint16_t));
	r_base = crtc->gamma_store;
	if (copyin((void *)(uintptr_t)crtc_lut->red, r_base, size)) {
		ret = -EFAULT;
		goto out;
	}

	g_base = (char *)r_base + size;
	if (copyin((void *)(uintptr_t)crtc_lut->green, g_base, size)) {
		ret = -EFAULT;
		goto out;
	}

	b_base = (char *)g_base + size;
	if (copyin((void *)(uintptr_t)crtc_lut->blue, b_base, size)) {
		ret = -EFAULT;
		goto out;
	}

	crtc->funcs->gamma_set(crtc, r_base, g_base, b_base, 0, crtc->gamma_size);

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;

}

int drm_mode_gamma_get_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv)
{
	struct drm_mode_crtc_lut *crtc_lut = data;
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	void *r_base, *g_base, *b_base;
	int size;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EINVAL;

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	obj = drm_mode_object_find(dev, crtc_lut->crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		ret = -EINVAL;
		goto out;
	}
	crtc = obj_to_crtc(obj);

	/* memcpy into gamma store */
	if (crtc_lut->gamma_size != crtc->gamma_size) {
		ret = -EINVAL;
		goto out;
	}

	size = crtc_lut->gamma_size * (sizeof(uint16_t));
	r_base = crtc->gamma_store;
	if (copyout(r_base, (void *)(uintptr_t)crtc_lut->red, size)) {
		ret = -EFAULT;
		goto out;
	}

	g_base = (char *)r_base + size;
	if (copyout(g_base, (void *)(uintptr_t)crtc_lut->green, size)) {
		ret = -EFAULT;
		goto out;
	}

	b_base = (char *)g_base + size;
	if (copyout(b_base, (void *)(uintptr_t)crtc_lut->blue, size)) {
		ret = -EFAULT;
		goto out;
	}
out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return ret;
}

static void
drm_kms_free(void *arg)
{

	drm_free(arg, DRM_MEM_KMS);
}

int drm_mode_page_flip_ioctl(struct drm_device *dev, void *data,
    struct drm_file *file_priv)
{
	struct drm_mode_crtc_page_flip *page_flip = data;
	struct drm_mode_object *obj;
	struct drm_crtc *crtc;
	struct drm_framebuffer *fb;
	struct drm_pending_vblank_event *e = NULL;
	int ret = EINVAL;

	if (page_flip->flags & ~DRM_MODE_PAGE_FLIP_FLAGS ||
	    page_flip->reserved != 0)
		return (EINVAL);

	lockmgr(&dev->mode_config.lock, LK_EXCLUSIVE);
	obj = drm_mode_object_find(dev, page_flip->crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!obj)
		goto out;
	crtc = obj_to_crtc(obj);

	if (crtc->fb == NULL) {
		/* The framebuffer is currently unbound, presumably
		 * due to a hotplug event, that userspace has not
		 * yet discovered.
		 */
		ret = EBUSY;
		goto out;
	}

	if (crtc->funcs->page_flip == NULL)
		goto out;

	obj = drm_mode_object_find(dev, page_flip->fb_id, DRM_MODE_OBJECT_FB);
	if (!obj)
		goto out;
	fb = obj_to_fb(obj);

	if (crtc->mode.hdisplay > fb->width ||
	    crtc->mode.vdisplay > fb->height ||
	    crtc->x > fb->width - crtc->mode.hdisplay ||
	    crtc->y > fb->height - crtc->mode.vdisplay) {
		DRM_DEBUG_KMS("Invalid fb size %ux%u for CRTC viewport %ux%u+%d+%d.\n",
			      fb->width, fb->height,
			      crtc->mode.hdisplay, crtc->mode.vdisplay,
			      crtc->x, crtc->y);
		ret = ENOSPC;
		goto out;
	}

	if (page_flip->flags & DRM_MODE_PAGE_FLIP_EVENT) {
		ret = ENOMEM;
		lockmgr(&dev->event_lock, LK_EXCLUSIVE);
		if (file_priv->event_space < sizeof e->event) {
			lockmgr(&dev->event_lock, LK_RELEASE);
			goto out;
		}
		file_priv->event_space -= sizeof e->event;
		lockmgr(&dev->event_lock, LK_RELEASE);

		e = kmalloc(sizeof *e, DRM_MEM_KMS, M_WAITOK | M_ZERO);

		e->event.base.type = DRM_EVENT_FLIP_COMPLETE;
		e->event.base.length = sizeof e->event;
		e->event.user_data = page_flip->user_data;
		e->base.event = &e->event.base;
		e->base.file_priv = file_priv;
		e->base.destroy =
			(void (*) (struct drm_pending_event *))drm_kms_free;
	}

	ret = -crtc->funcs->page_flip(crtc, fb, e);
	if (ret != 0) {
		if (page_flip->flags & DRM_MODE_PAGE_FLIP_EVENT) {
			lockmgr(&dev->event_lock, LK_EXCLUSIVE);
			file_priv->event_space += sizeof e->event;
			lockmgr(&dev->event_lock, LK_RELEASE);
			drm_free(e, DRM_MEM_KMS);
		}
	}

out:
	lockmgr(&dev->mode_config.lock, LK_RELEASE);
	return (ret);
}

void drm_mode_config_reset(struct drm_device *dev)
{
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	struct drm_connector *connector;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		if (crtc->funcs->reset)
			crtc->funcs->reset(crtc);

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head)
		if (encoder->funcs->reset)
			encoder->funcs->reset(encoder);

	list_for_each_entry(connector, &dev->mode_config.connector_list, head)
		if (connector->funcs->reset)
			connector->funcs->reset(connector);
}

int drm_mode_create_dumb_ioctl(struct drm_device *dev,
			       void *data, struct drm_file *file_priv)
{
	struct drm_mode_create_dumb *args = data;

	if (!dev->driver->dumb_create)
		return -ENOTSUP;
	return dev->driver->dumb_create(file_priv, dev, args);
}

int drm_mode_mmap_dumb_ioctl(struct drm_device *dev,
			     void *data, struct drm_file *file_priv)
{
	struct drm_mode_map_dumb *args = data;

	/* call driver ioctl to get mmap offset */
	if (!dev->driver->dumb_map_offset)
		return -ENOTSUP;

	return dev->driver->dumb_map_offset(file_priv, dev, args->handle, &args->offset);
}

int drm_mode_destroy_dumb_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file_priv)
{
	struct drm_mode_destroy_dumb *args = data;

	if (!dev->driver->dumb_destroy)
		return -ENOTSUP;

	return dev->driver->dumb_destroy(file_priv, dev, args->handle);
}

/*
 * Just need to support RGB formats here for compat with code that doesn't
 * use pixel formats directly yet.
 */
void drm_fb_get_bpp_depth(uint32_t format, unsigned int *depth,
			  int *bpp)
{
	switch (format) {
	case DRM_FORMAT_RGB332:
	case DRM_FORMAT_BGR233:
		*depth = 8;
		*bpp = 8;
		break;
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_RGBX5551:
	case DRM_FORMAT_BGRX5551:
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_BGRA5551:
		*depth = 15;
		*bpp = 16;
		break;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		*depth = 16;
		*bpp = 16;
		break;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		*depth = 24;
		*bpp = 24;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_BGRX8888:
		*depth = 24;
		*bpp = 32;
		break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_RGBX1010102:
	case DRM_FORMAT_BGRX1010102:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_BGRA1010102:
		*depth = 30;
		*bpp = 32;
		break;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRA8888:
		*depth = 32;
		*bpp = 32;
		break;
	default:
		DRM_DEBUG_KMS("unsupported pixel format\n");
		*depth = 0;
		*bpp = 0;
		break;
	}
}
