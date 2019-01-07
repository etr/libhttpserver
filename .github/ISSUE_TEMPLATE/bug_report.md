---
name: Bug report
about: Create a report to help us improve
title: "[BUG] Title"
labels: bug
assignees: etr

---

<!--

Have you read Atom's Code of Conduct? By filing an Issue, you are expected to comply with it, including treating everyone with respect: https://github.com/etr/libhttpserver/blob/master/CODE_OF_CONDUCT.md

Do you want to ask a question? Are you looking for support? Our message board is the best place for getting support: https://gitter.im/libhttpserver/community

-->

### Prerequisites

* [ ] Put an X between the brackets on this line if you have checked that your issue isn't already filed: https://github.com/search?l=&q=repo%3Aetr%2Flibhttpserver&type=Issues

### Description

[Description of the issue]

### Steps to Reproduce

1. [First Step]
2. [Second Step]
3. [and so on...]

**Expected behavior:** [What you expect to happen]

**Actual behavior:** [What actually happens]

**Reproduces how often:** [What percentage of the time does it reproduce?]

### Versions

* OS version (if on linux, the output of "uname -a")
* libhttpserver version (please specify whether compiled or packaged)
* libmicrohttpd version (please specify whether compiled or packaged)

If you have problems during build:
* Compiler version
* autotools version

### Additional Information

Any additional information, configuration (especially build configuration flags if you compiled the libraries) or data that might be necessary to reproduce the issue.

If you have problems during build, please attach your config.log and the full scope of your error from make.

If you have problems at execution, please:
* attach the stacktrace in case of crash (a coredump would be even better).
* provide a main that reproduces the error.
