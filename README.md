# netfork

A research project which provides functionality of forking a Windows process over the internet.

Netforking works similarly to POSIX `fork`. For example, run a process on machine A, call `netfork::fork` in that process, and it will continue executing at the call site on machine B. The process running on machine A will return from the call site and continue execution.

## Security Implications

Since the netfork server is meant to reconstruct a process given the data received from the client, it is possible to execute malicious processes. Be careful!

## Limitations

* Tested on Windows 10 only.
* Potentially will not work with machines running different versions of Windows.
* Since netfork is not running in kernel-space, it can't perfectly recreate a process' memory address space, and so may fail.
* Single-threaded forking only.
* Sharing resources between forking boundaries will not work.
    * e.g. It's not possible to duplicate opened file handles.
* It was not made with security in mind.
* Very large processes will probably take a long time to netfork; or there will likely be a higher chance of failure.
