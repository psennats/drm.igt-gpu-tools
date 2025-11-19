# :material-plus-circle: Add support for new driver in IGT

Here is a detailed procedure to support a new driver in IGT, using Intel's "XE" driver as
an example.

## Detect the Driver

Add support in IGT lib to detect the driver, so that IGT will recognize the driver. The
device name should match with the DRIVER_NAME configured in the Linux Kernel Module.

Example:
[drm/xe/kernel/-/blob/drm-xe-next/drivers/gpu/drm/xe/xe_drv.h#L11](https://gitlab.freedesktop.org/drm/xe/kernel/-/blob/drm-xe-next/drivers/gpu/drm/xe/xe_drv.h#L11)

### KMD Changes:

```c
#define DRIVER_NAME     "xe"

#define DRIVER_DESC     "Intel Xe Graphics"

#define DRIVER_DATE     "20201103"
```

### IGT Changes:

```diff
diff --git a/lib/drmtest.c b/lib/drmtest.c

index 8e2d1ac50b..0ceab10389 100644

--- a/lib/drmtest.c

+++ b/lib/drmtest.c

@@ -189,6 +189,7 @@  static const struct module {

 	{ DRIVER_V3D, "v3d" },

 	{ DRIVER_VC4, "vc4" },

 	{ DRIVER_VGEM, "vgem" },

+	{ DRIVER_XE, "xe" },

 	{}

 };



@@ -547,6 +548,8 @@  static const char *chipset_to_str(int chipset)

 		return "panfrost";

 	case DRIVER_MSM:

 		return "msm";

+	case DRIVER_XE:

+		return "xe";

 	case DRIVER_ANY:

 		return "any";

 	default:

diff --git a/lib/drmtest.h b/lib/drmtest.h

index b5debd44b3..448ac03b49 100644

--- a/lib/drmtest.h

+++ b/lib/drmtest.h

@@ -51,6 +51,7 @@

 #define DRIVER_V3D	(1 << 4)

 #define DRIVER_PANFROST	(1 << 5)

 #define DRIVER_MSM	(1 << 6)

+#define DRIVER_XE	(1 << 7)
```


With the above changes, the simple IGT snippet `int fd = drm_open_driver_master(DRIVER_XE);`
can open the "XE" driver.

## :material-code-braces: UAPI Changes to Support Driver-Specific IOCTLs

Import UAPI headers: https://gitlab.freedesktop.org/drm/igt-gpu-tools#includedrm-uapi

## :material-wrench: Develop IGT Helpers

Now we can develop driver-specific IGT helpers based on DRM_IOCTL_VERSION.

Example:

```diff
+++ b/lib/drmtest.c

@@ -139,6 +139,16 @@  bool is_vc4_device(int fd)

 	return __is_device(fd, "vc4");

 }



+bool is_xe_device(int fd)

+{

+	return __is_device(fd, "xe");

+}



+++ b/lib/intel_chipset.c

@@ -125,22 +163,18 @@  intel_get_pci_device(void)

 uint32_t

 intel_get_drm_devid(int fd)

 {



+	if (is_xe_device(fd))

+		return __xe_get_drm_devid(fd);

+	else

+		return __i915_get_drm_devid(fd);

}
```

## :material-monitor: Porting All Display IGTs to Support XE Driver

The task of porting all display-specific IGTs to support the XE driver is significant and
critical. This process involves several steps.

### :material-eye: Understanding the Current IGTs

The existing codebase must be thoroughly understood. This involves reviewing the code for
all display IGTs, understanding their functionality, and identifying any potential issues
that may arise when porting to the XE driver.

!!! info "Challenges"
    As there are several hundreds of display subtests, it's always a challenge to
    read/review the codebase.

### :material-chip: Understanding the XE Driver

The XE driver's specifications and capabilities must be understood. This will allow for
the identification of any features that the IGTs can take advantage of, as well as any
limitations that must be worked around.

!!! info "Challenges"
    Identify all i915 display-specific ioctls and find the corresponding ioctl in XE.

    Different approach of using modparams and missing debugfs.

### :material-map: Mapping the IGTs to the XE Driver

Once we understand both the IGTs and the XE driver, we can start mapping the functionality
of the IGTs to the XE driver. This involves identifying which tests can be supported by
the XE driver, which ones need to be modified, and which ones cannot be supported.

### :material-library: Modifying IGT Libraries

Start modifying the IGT libraries to support the XE driver and try to make the APIs as
unified (to support both i915 and XE). To achieve this, collaborate and contribute with
the core-mm and other development teams.

!!! info "Challenges"
    Make the core team understand the display use cases to write unified APIs
    (Ex: VM bind, GPU HANG, and BUSY).

    Integrate XE-specific APIs with existing IGT display libraries (Ex: BO creation).

**Notable contributions by Bhanu:**

- Add IGT helpers to check for the XE driver
- Add an interface to query dev_id
- Get and cache the XE_config at driver opening level and uncache at driver closing level
- Support to create a BO for display framebuffers
- Add rendercopy support for display tests
- Add tiling support for XE
- API support to get the pipe index

### :material-code-tags: Modifying the IGTs

Based on the mapping, we can start modifying the IGTs to support the XE driver. This
could involve changing the code, adding new lib helpers, adding new tests, or removing
unsupported tests. This process involves the following steps:

- Modify test initialization code to open the XE driver instead of Intel where applicable
- Update any Intel-specific code/defines in the tests to use equivalent XE APIs
- Ensure all relevant XE driver features are properly exercised by the tests. Add any
  missing test coverage
- Update documentation like man pages, comments, etc. to cover XE driver usage

!!! info "Challenges"
    Some KMS tests are complex and very difficult to update.

### :material-test-tube: Testing the Ported IGTs

After the IGTs have been modified, they need to be tested to ensure they work correctly
with the XE driver. This involves running the tests, analyzing the results, and fixing
any issues that are identified.

## :material-file-document: Test-Plan Documentation

With the addition of the Xe driver, a new way to document tests was added. It is based on
special comment-like annotation inside the C code. We need to update each and every IGT
with this new style of documentation.

!!! info "Challenges"
    - Time-consuming process, as we have several hundreds of subtests
    - Add missing documentation in existing legacy way
    - Identify different features and align with several internal tools like
      feature-mapping, Grafana, etc.
