specific_ss.add(when: 'CONFIG_SOFTMMU', if_true: [files(
  'arch_init.c',
  'balloon.c',
  'cpu-throttle.c',
  'cpu-timers.c',
  'cpus.c',
  'datadir.c',
  'globals.c',
  'ioport.c',
  'memory_mapping.c',
  'memory.c',
  'physmem.c',
  'qtest.c',
  'rtc.c',
  'runstate-action.c',
  'runstate.c',
  'vl.c',
)])

specific_ss.add(when: ['CONFIG_SOFTMMU', 'CONFIG_TCG'], if_true: [files(
  'icount.c'
)])

softmmu_ss.add(files(
  'bootdevice.c',
  'dma-helpers.c',
  'qdev-monitor.c',
), sdl, libpmem, libdaxctl)

softmmu_ss.add(when: 'CONFIG_TPM', if_true: files('tpm.c'))
softmmu_ss.add(when: seccomp, if_true: files('qemu-seccomp.c'))
softmmu_ss.add(when: fdt, if_true: files('device_tree.c'))
