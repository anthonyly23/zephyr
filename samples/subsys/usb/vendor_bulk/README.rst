.. zephyr:code-sample:: vendor-bulk
   :name: USB vendor-class bulk throughput sample
   :relevant-api: usbd_api

   High-throughput USB vendor-class bulk endpoint transport for the device-next
   USB stack, with a Python (PyUSB / libusb1) host benchmark.

Overview
========

This sample exposes the device as a single USB vendor-class function with one
bulk **IN** endpoint (``0x81``) and one bulk **OUT** endpoint (``0x01``). The
firmware continuously streams a fixed test pattern on the bulk IN endpoint and
discards anything received on the bulk OUT endpoint.

It is intended as:

* A reference for building custom vendor-class USB transports on top of the
  Zephyr device-next USB stack (``CONFIG_USB_DEVICE_STACK_NEXT``).
* A throughput benchmark for the underlying UDC driver. On the
  ``kit_pse84_eval`` board (Cortex-M55 + DesignWare DWC2 USB-HS in Buffer DMA
  mode) it sustains ~13 MiB/s of bulk IN — the structural ceiling for that
  configuration.

The class driver shipped in this sample (``src/vendor_bulk.c``) is a
trimmed-down fork of the upstream
``zephyr/subsys/usb/device_next/class/loopback.c``. It lives inside the
sample directory so it can be fork and modify it without patching the
Zephyr tree.

Components
==========

::

   samples/subsys/usb/vendor_bulk/
   ├── CMakeLists.txt
   ├── Kconfig                     Sample-specific options
   ├── README.rst                  This file
   ├── app.overlay                 Enables zephyr_udc0 on the target board
   ├── prj.conf                    Project config (USB stack, pool sizing, etc.)
   ├── sample.yaml                 Twister metadata
   ├── vendor_bulk_speed_test.py   Host-side throughput benchmark (Python)
   └── src/
       ├── main.c                  App: enable USB, refill the IN pipeline
       ├── vendor_bulk.c           Custom vendor-class driver (descriptors + EPs)
       └── vendor_bulk.h           Public API of the class driver

Firmware design
===============

USB topology
------------

Single configuration with a single interface (``bInterfaceClass = 0xFF``,
vendor specific). Two endpoints:

==================  ======  ========================================
Endpoint            Type    Purpose
==================  ======  ========================================
``0x81`` (IN)       Bulk    Device → Host. 512 B max packet (HS).
``0x01`` (OUT)      Bulk    Host → Device. 512 B max packet (HS).
==================  ======  ========================================

There is no class-specific control request, no alternate setting, no
SET_INTERFACE handling beyond the defaults.  The ``USBD_DEFINE_CLASS()`` macro
in ``vendor_bulk.c`` registers the class with the device-next USB core and
gives it descriptors for both Full-Speed (64 B mps) and High-Speed (512 B
mps).

Data path
---------

In producer mode (``CONFIG_APP_VENDOR_BULK_PRODUCER=y``, the default):

1. ``main.c`` fills a single source buffer (``in_buf[XFER_SIZE]``) with a
   deterministic counter pattern at boot.
2. When the host enables the configuration the class driver calls
   ``vendor_bulk_on_enable()`` which submits ``IN_DEPTH`` IN transfers.
3. The DWC2 controller transmits each transfer over USB. On completion it
   raises an interrupt; the UDC driver delivers a request-completion
   callback into ``vb_request_handler()``.
4. ``vb_request_handler()`` decrements the in-flight counter and calls
   ``vendor_bulk_on_in_complete()``, which immediately resubmits to keep
   ``IN_DEPTH`` transfers permanently queued.

In loopback mode (``CONFIG_APP_VENDOR_BULK_PRODUCER=n``) the class driver
echoes each OUT payload back on IN and the application thread does nothing.

Why pipelining matters
----------------------

USB bulk throughput is limited by the round-trip latency between
"transfer complete" and "next transfer armed". With a single transfer in
flight the controller spends most of the bus idle while software wakes up,
runs the completion handler, and submits the next request.  By keeping
``IN_DEPTH`` (4 by default) transfers always queued, the controller transmits
back-to-back transactions and software latency is hidden.

Cache coherency
---------------

The Cortex-M55 D-cache is enabled. Without intervention the DWC2 driver would
have to call ``sys_cache_data_flush_range()`` on every IN buffer before
handing it to the controller, costing real time at high data rates.

The sample sets ``CONFIG_NOCACHE_MEMORY=y`` and
``CONFIG_UDC_BUF_FORCE_NOCACHE=y`` so the UDC buffer pool lives in a
non-cached SRAM region. Cache maintenance becomes a no-op.

Public API (vendor_bulk.h)
--------------------------

.. code-block:: c

   int  vendor_bulk_in_submit(const uint8_t *payload, size_t len);
   int  vendor_bulk_in_inflight(void);
   bool vendor_bulk_is_ready(void);

   /* App-side hooks called from USB context. */
   void vendor_bulk_on_enable(void);
   void vendor_bulk_on_in_complete(void);

To replace the test pattern with real application data, change the body of
``main.c`` to fill ``in_buf`` from your data source before each
``vendor_bulk_in_submit()`` call. Note that the hooks run in the USB stack
thread; for heavy producers, post a kernel event from the hook and do the
heavy lifting in your own thread.

Configuration knobs
===================

In ``prj.conf``:

==========================================  =======  =================================
Symbol                                      Default  Effect
==========================================  =======  =================================
``CONFIG_APP_VENDOR_BULK_PRODUCER``         y        Producer (y) vs loopback (n)
``CONFIG_APP_VENDOR_BULK_XFER_SIZE``        16384    Bytes per transfer
``CONFIG_APP_VENDOR_BULK_IN_DEPTH``         4        Pipelined IN transfers
``CONFIG_UDC_BUF_COUNT``                    32       Net-buf slots in UDC pool
``CONFIG_UDC_BUF_POOL_SIZE``                98304    Total bytes in UDC pool
``CONFIG_UDC_BUF_FORCE_NOCACHE``            y        Place pool in non-cached SRAM
``CONFIG_SAMPLE_USBD_VID``                  0x2fe3   Zephyr-project test VID
``CONFIG_SAMPLE_USBD_PID``                  0x000a   Sample PID
==========================================  =======  =================================

The pool must be large enough to hold ``IN_DEPTH × XFER_SIZE`` bytes (64 KiB
in the default config) plus some headroom for control transfers.

.. note::
   The upstream ``CONFIG_UDC_BUF_POOL_SIZE`` Kconfig range is 64 .. 32768. The
   sample's repository ships a small patch to ``zephyr/drivers/usb/udc/Kconfig``
   that raises the upper bound to 1 MiB so ``UDC_BUF_POOL_SIZE=98304`` is
   accepted.

Building and flashing
=====================

Building (PowerShell example for the Infineon PSE84 evaluation board)::

   ./.venv/Scripts/west.exe build -p -b kit_pse84_eval/pse846gps2dbzc4a/m55 \
       zephyr/samples/subsys/usb/vendor_bulk -d cm55_vendor_bulk
   ./.venv/Scripts/west.exe flash -d cm55_vendor_bulk

For other boards, simply substitute ``-b <your-board>``. Any board whose
device tree binds ``zephyr_udc0`` to a UDC driver supported by the
device-next stack should work; you may need an additional ``app.overlay``
that enables the controller node, similar to the one shipped here.

Host setup (Windows)
====================

The device enumerates as a vendor-class device (``0xff/0x00/0x00``). Windows
does not load a default driver for vendor-class interfaces, so you must bind
WinUSB once with `Zadig <https://zadig.akeo.ie>`_:

1. Plug the board in. Windows will report an unknown device.
2. Open Zadig → **Options → List All Devices**.
3. Select **"Zephyr vendor bulk sample"** (interface 0).
4. In the right-hand driver dropdown choose **WinUSB** and click
   **Install Driver**.
5. After installation Device Manager should list the device under
   **Universal Serial Bus devices**.

Install the Python dependencies inside the project venv::

   pip install pyusb libusb-package libusb1

Host setup (Linux)
==================

On Linux ``libusb`` can claim the device directly. You may want a udev rule to
grant your user access without sudo::

   # /etc/udev/rules.d/99-zephyr-vendor-bulk.rules
   SUBSYSTEM=="usb", ATTRS{idVendor}=="2fe3", ATTRS{idProduct}=="000a", MODE="0666"

Then ``sudo udevadm control --reload && sudo udevadm trigger``.

Host benchmark script
=====================

``vendor_bulk_speed_test.py`` (shipped alongside the firmware sources in
``samples/subsys/usb/vendor_bulk/``) measures one or both directions of the
endpoint and prints sustained throughput in MiB/s.

Quickstart
----------

From the project root, after building/flashing the firmware and binding
WinUSB (Windows) or installing the udev rule (Linux)::

   python zephyr\samples\subsys\usb\vendor_bulk\vendor_bulk_speed_test.py \
       --pid 0x000a --mode in-async --queue 8 --xfer 65536

Expected output on the ``kit_pse84_eval`` board with the recommended
firmware config (``XFER_SIZE=32768``, ``IN_DEPTH=4``)::

   Device 0x2fe3:0x000a interface 0 (0xff/0x00/0x00)
     EP 0x01 mps=512
     EP 0x81 mps=512
   [ASY] reading from EP 0x81, xfer=65536B, queue=8, 10.0s ...
   [ASY] ~150 MiB in 11.1s = ~13.7 MiB/s

CLI summary
-----------

::

   python vendor_bulk_speed_test.py [options]

   --vid 0xVVVV         Device vendor ID (default 0x2fe3)
   --pid 0xPPPP         Device product ID (default 0x000a)
   --interface N        Interface number to claim (default 0)
   --ep-out 0xEE        Bulk OUT endpoint address (default 0x01)
   --ep-in  0xEE        Bulk IN  endpoint address (default 0x81)
   --xfer  N            Bytes per USB transfer (default 65536)
   --seconds S          Test duration per direction (default 10)
   --queue N            Async queue depth for in-async mode (default 8)
   --timeout MS         Per-transfer timeout in ms (default 5000)
   --mode <m>           in | out | both | in-async (default both)

Modes
-----

``--mode in``
    Issues sequential synchronous reads from the IN endpoint via PyUSB
    (``dev.read``). Simple but only one transfer is ever in flight, so it is
    bounded by host round-trip latency.

``--mode out``
    Sequential synchronous writes to the OUT endpoint. The firmware discards
    the data; this measures pure host→device bulk OUT throughput.

``--mode both``
    Spawns two threads — one writer, one reader — and runs them concurrently
    for the requested duration. Reports per-direction MiB/s.

``--mode in-async``  *(recommended for IN throughput)*
    Uses ``python-libusb1`` directly instead of PyUSB. Submits ``--queue``
    asynchronous bulk reads, and resubmits each one from its completion
    callback so the host always has ``--queue`` reads pending in the kernel
    URB queue. This eliminates host-side serialisation and is the correct way
    to measure the device's true sustained IN throughput.

What the script actually does
-----------------------------

1. **Enumerate**: ``libusb_package.get_libusb1_backend()`` provides a backend
   that works on Windows without a separate libusb DLL install.
   ``usb.core.find(idVendor, idProduct)`` locates the device.
2. **Claim**: detach any kernel driver (Linux only), call
   ``set_configuration()`` and ``claim_interface(0)``. Endpoint descriptors
   are printed so you can see the negotiated max-packet size and confirm HS.
3. **Time**: the chosen benchmark function runs for ``--seconds`` and
   accumulates the byte count of every successful transfer.
4. **Report**: total MiB transferred and MiB/s. For ``in-async`` the elapsed
   time may slightly exceed ``--seconds`` because the script waits for
   in-flight transfers to be cancelled cleanly before stopping the timer.

Example
-------

::

   $ python zephyr\samples\subsys\usb\vendor_bulk\vendor_bulk_speed_test.py \
         --pid 0x000a --mode in-async --queue 8 --xfer 65536
   Device 0x2fe3:0x000a interface 0 (0xff/0x00/0x00)
     EP 0x01 mps=512
     EP 0x81 mps=512
   [ASY] reading from EP 0x81, xfer=65536B, queue=8, 10.0s ...
   [ASY] 145.69 MiB in 11.11s = 13.114 MiB/s

To verify the link came up at High-Speed::

   python -c "import libusb_package, usb.core; \
              d = usb.core.find(idVendor=0x2fe3, idProduct=0x000a, \
                                backend=libusb_package.get_libusb1_backend()); \
              print('speed:', d.speed)"

PyUSB returns ``1=LS, 2=FS, 3=HS, 4=SS``. Anything other than 3 on this
sample means cabling, host port, or PHY is forcing a slower speed.

Throughput notes
================

Reference points on the same ``kit_pse84_eval`` hardware:

* CDC-ACM echo on the same hardware peaks around **2–3 MiB/s**.
* CDC-ACM one-way TX peaks around **5 MiB/s**.
* This vendor-bulk sample with pipelining and non-cached buffers peaks around
  **13 MiB/s**.

Measured BufferDMA ceiling
--------------------------

A transfer-size / pipeline-depth sweep was run with ``--mode in-async
--queue 8`` against the firmware, varying ``CONFIG_APP_VENDOR_BULK_XFER_SIZE``
and ``CONFIG_APP_VENDOR_BULK_IN_DEPTH`` (pool size adjusted accordingly):

==========  ==========  =========  ============  ===============
Config      XFER_SIZE   IN_DEPTH   Pool (B)      Measured
==========  ==========  =========  ============  ===============
A           16 KiB      4          98 304        ~13.0 MiB/s
B           32 KiB      4          163 840       ~13.7 MiB/s
D           64 KiB      1          81 920        **0 MiB/s** (stall)
==========  ==========  =========  ============  ===============

Two things to note:

* **Config B (32 KiB × 4) is the practical optimum on this SoC.** Going
  from 16 KiB to 32 KiB delivers a small (~5 %) gain; further increases do
  not help.
* **Config D (single 64 KiB transfer) stalls completely.** The DWC2
  controller's per-transfer ``XferSize`` field has a finite width (see
  ``dwc2_get_iept_xfersize()`` and ``GHWCFG3_XFERSIZE`` in
  ``udc_dwc2.c``). On many DWC2 instantiations the IN endpoint cap is
  19 bits (524 287 bytes) but a per-buffer cap of 32 767 bytes is also
  common. When the requested length exceeds the controller's hardware cap,
  ``dwc2_prep_tx`` rounds the transfer down to a multiple of MPS, which
  combined with ``IN_DEPTH=1`` and host-side ``--xfer 65536`` leaves the
  pipeline waiting for completions that never arrive, so the host reports
  ``0 MiB/s`` until timeout. **Stay at or below 32 KiB per transfer for
  this driver.**

Implications for the throughput model
-------------------------------------

A naive software-overhead model would predict throughput to scale with
transfer size (rate ≈ ``size / (T_sw + T_bus)``, where ``T_sw`` is the
per-transfer software round-trip and ``T_bus = size / 53 MiB/s``). The
measured numbers from configs A and B are essentially flat (13.0 vs
13.7 MiB/s for a 2× transfer-size change), and D fails outright. The
bottleneck is therefore **not** per-transfer software overhead. Something
that scales per byte (or per packet) caps the link at ~13–14 MiB/s, and
the path past it is **not** "use bigger transfers".

Why BufferDMA caps at ~13 MiB/s
-------------------------------

In *Buffer DMA* mode the DWC2 controller transfers one buffer per IN
endpoint at a time. Between transfers the IN-FIFO must be re-armed by the
UDC driver's worker thread (interrupt → thread wakeup → completion
callback → endpoint re-arm). Because IN packets are not back-to-back across
transfer boundaries, and because the worker thread refills the FIFO at a
rate the AHB/PHY combination can sustain, the wire never sees a fully
saturated stream. Increasing transfer size only makes each contiguous burst
longer at the same fill rate; the average is unchanged.

Other contributors that scale per byte (and therefore would not be relieved
by larger transfers) include AHB bus arbitration on this SoC and the
silicon-specific effective USB-HS PHY bandwidth (typically ~25–30 MiB/s
practical, not the theoretical ~53 MiB/s).

Path past the ceiling
---------------------

The DWC2 controller also supports *Descriptor DMA* mode, in which a chain
of transfer descriptors is consumed autonomously by hardware and only one
interrupt is raised at the end of the chain. This eliminates the
per-transfer software round-trip entirely.

Zephyr's upstream UDC driver explicitly disables Descriptor DMA — see
``USB_DWC2_DCFG_DESCDMA`` being cleared in
``zephyr/drivers/usb/udc/udc_dwc2.c`` (around line 1848). Enabling DDMA in
the driver (vendor patch) is the only known way to push past the
~13 MiB/s ceiling without changing the OS or USB stack on this SoC.

License
=======

Apache-2.0. Derived from upstream Zephyr ``loopback.c``
(SPDX: Apache-2.0, copyright Nordic Semiconductor ASA).
