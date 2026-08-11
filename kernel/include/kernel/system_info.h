#ifndef KERNEL_SYSTEM_INFO_H
#define KERNEL_SYSTEM_INFO_H

/* Print a snapshot of CPU, scheduler, and kernel resource information. */
void system_info_print(void);

/* Print changing scheduler/resource data without repeating CPUID details. */
void system_info_print_live(void);

#endif
