# ESK Kernel

![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/bachnxuan/gki_kernel_builder/release.yml) [![Telegram](https://img.shields.io/badge/Telegram-2CA5E0?logo=telegram&logoColor=white)](https://t.me/xaga_kernel)

> [!IMPORTANT]
> We are moving to [ESK Project Organization](https://github.com/ESK-Project), please update your repository reference accordingly to get futures update. 

Custom kernel for **xaga(in)** / **xagapro(in)**. Supports **AOSP** and **HyperOS**.

---

## Download

Get the latest **Boot Image** / **AnyKernel3 zip** from the release: [Github Release](https://github.com/bachnxuan/android12-5.10-lts/releases/latest)

---

## How to flash

### Fastboot (Boot Image)

> [!NOTE]
> You may need to flash to `boot_a` / `boot_b` according to your current slot.

```sh
fastboot flash boot <boot.img>
fastboot reboot
```

### AnyKernel3 (Recommended)

Flash with your favorite kernel flasher (FKM, Kernel Flasher, Horizon, …).

---

## Builds

All builds are produced by the kernel builder:
[`bachnxuan/gki_kernel_builder`](https://github.com/bachnxuan/gki_kernel_builder)

---

## Notes

* Kernel source is in the [`esk/main`](https://github.com/bachnxuan/android12-5.10-lts/tree/esk/main) branch.
* When reporting issues, include device, ROM + Android version, build tag, and logs (dmesg/last\_kmsg).
