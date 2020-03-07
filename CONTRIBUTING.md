# Contributing to libhttpserver

:+1::tada: First off, thanks for taking the time to contribute! :tada::+1:

The following is a set of guidelines for contributing to libhttpserver. These are mostly guidelines, not rules. Use your best judgment, and feel free to propose changes to this document in a pull request.

#### Table Of Contents

[Code of Conduct](#code-of-conduct)

[I don't want to read this whole thing, I just have a question!!!](#i-dont-want-to-read-this-whole-thing-i-just-have-a-question)

[How Can I Contribute?](#how-can-i-contribute)
  * [Reporting Bugs](#reporting-bugs)
  * [Suggesting Enhancements](#suggesting-enhancements)
  * [Your First Code Contribution](#your-first-code-contribution)
  * [Pull Requests](#pull-requests)

[Styleguides](#styleguides)
  * [Git Commit Messages](#git-commit-messages)
  * [Documentation Styleguide](#documentation-styleguide)

[Additional Notes](#additional-notes)
  * [Issue and Pull Request Labels](#issue-and-pull-request-labels)

## Code of Conduct

This project and everyone participating in it is governed by the [Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code. Please report unacceptable behavior to [merlino.sebastiano@gmail.com](mailto:merlino.sebastiano@gmail.com).

## I don't want to read this whole thing I just have a question!!!

> **Note:** Please don't file an issue to ask a question. You'll get faster results by using the resources below.

We have an official community board where the community chimes in with helpful advice if you have questions.

* [libhttpserver on Gitter](https://gitter.im/libhttpserver/community)

## How Can I Contribute?

### Reporting Bugs

This section guides you through submitting a bug report for libhttpserver. Following these guidelines helps maintainers and the community understand your report :pencil:, reproduce the behavior :computer: :computer:, and find related reports :mag_right:.

Before creating bug reports, please check [this list](#before-submitting-a-bug-report) as you might find out that you don't need to create one. When you are creating a bug report, please [include as many details as possible](#how-do-i-submit-a-good-bug-report). Fill out [the required template](https://github.com/etr/libhttpserver/blob/master/.github/ISSUE_TEMPLATE/bug_report.md), the information it asks for helps us resolve issues faster.

> **Note:** If you find a **Closed** issue that seems like it is the same thing that you're experiencing, open a new issue and include a link to the original issue in the body of your new one.

#### Before Submitting A Bug Report

* **Try to debug the problem** You might be able to find the cause of the problem and fix things yourself. Most importantly, check if you can reproduce the problem in the latest version of libhttpserver (head on github).
* **Perform a [cursory search](https://github.com/search?l=&q=repo%3Aetr%2Flibhttpserver&type=Issues)** to see if the problem has already been reported. If it has **and the issue is still open**, add a comment to the existing issue instead of opening a new one.

#### How Do I Submit A (Good) Bug Report?

Bugs are tracked as [GitHub issues](https://guides.github.com/features/issues/). After you followed the steps above, create an issue and provide the following information by filling in [the template](https://github.com/etr/libhttpserver/blob/master/.github/ISSUE_TEMPLATE/bug_report.md).

Explain the problem and include additional details to help maintainers reproduce the problem:

* **Use a clear and descriptive title** for the issue to identify the problem.
* **Describe the exact steps which reproduce the problem** in as many details as possible. When listing steps, **don't just say what you did, but explain how you did it**.
* **Provide specific examples to demonstrate the steps**. Include links to files or GitHub projects, or copy/pasteable snippets, which you use in those examples. If you're providing snippets in the issue, use [Markdown code blocks](https://help.github.com/articles/markdown-basics/#multiple-lines).
* **Describe the behavior you observed after following the steps** and point out what exactly is the problem with that behavior.
* **Explain which behavior you expected instead and why.**
* **If you're reporting a crash**, include a crash report with a stack trace from the operating system. Include these in the issue in a [code block](https://help.github.com/articles/markdown-basics/#multiple-lines), a [file attachment](https://help.github.com/articles/file-attachments-on-issues-and-pull-requests/), or put it in a [gist](https://gist.github.com/) and provide link to that gist.
* **Consider attaching a simple snipped reproducing the problem. **
* **If the problem is related to performance or memory**, include a CPU profile capture with your report.

Provide more context by answering these questions:

* **Did the problem start happening recently** (e.g. after updating to a new version of libhttpserver) or was this always a problem?
* If the problem started happening recently, **can you reproduce the problem in an older version of libhttpserver?** What's the most recent version in which the problem doesn't happen? You can download older versions of libhttpserver from [the releases page](https://github.com/etr/libhttpserver/releases).
* **Can you reliably reproduce the issue?** If not, provide details about how often the problem happens and under which conditions it normally happens.

Include details about your configuration and environment:

* **Which version of libhttpserver are you using?**
* **What's the name and version of the OS you're using (e.g. "uname -a" on linux) **?
* **What's the version of libmicrohttpd that you have installed? **
* **Have you installed the libraries (both libhttpserver and libmicrohttpd) manually or through package manager? **
* **Which options did you use when compiling? **
* **What compiler version and version of autotools did you use? **

### Feature Requests and Enhancements

This section guides you through submitting an enhancement suggestion for libhttpserver, including completely new features and minor improvements to existing functionality. Following these guidelines helps maintainers and the community understand your suggestion and find related suggestions.

Before creating enhancement suggestions, please check [this list](#before-submitting-an-enhancement-suggestion) as you might find out that you don't need to create one. When you are creating an enhancement suggestion, please [include as many details as possible](#how-do-i-submit-a-good-enhancement-suggestion). Fill in [the template](https://github.com/etr/libhttpserver/blob/master/.github/ISSUE_TEMPLATE/feature_request.md).

#### Before Submitting An Enhancement Suggestion or a Feature Request

* **Perform a [cursory search](https://github.com/search?l=&q=repo%3Aetr%2Flibhttpserver&type=Issues)** to see if the enhancement has already been suggested. If it has, add a comment to the existing issue instead of opening a new one.

#### How Do I Submit A (Good) Feature Request / Enhancement Suggestion?

Enhancement suggestions are tracked as [GitHub issues](https://guides.github.com/features/issues/). Create an issue on that repository and provide the following information:

* **Use a clear and descriptive title** for the issue to identify the suggestion.
* **Provide a step-by-step description of the suggested enhancement** in as many details as possible.
* **Provide a specific example to demonstrate the new feature**.
* **Describe the current behavior** and **explain which behavior you expected instead** and why.
* **Describe which alternatives you have considered**.
* **Explain why this enhancement would be useful** to most users and **why it fits the mission of the library**.

### Your First Code Contribution

Unsure where to begin contributing to libhttpserver? You can start by looking through these `beginner` and `help-wanted` issues:

* [Beginner issues][beginner] - issues which should only require a few lines of code, and a test or two.
* [Help wanted issues][help-wanted] - issues which should be a bit more involved than `beginner` issues.

Both issue lists are sorted by total number of comments. While not perfect, number of comments is a reasonable proxy for impact a given change will have.

### Pull Requests

The process described here has several goals:

- Maintain libhttpserver's quality
- Fix problems that are important to users
- Engage the community in working toward the best possible solution
- Enable a sustainable system for maintainers to review contributions

Please follow these steps to have your contribution considered by the maintainers:

1. Follow all instructions in [the template](https://github.com/etr/libhttpserver/blob/master/PULL_REQUEST_TEMPLATE.md)
2. Follow the [styleguides](#styleguides)
3. After you submit your pull request, verify that all [status checks](https://help.github.com/articles/about-status-checks/) are passing <details><summary>What if the status checks are failing?</summary>If a status check is failing, and you believe that the failure is unrelated to your change, please leave a comment on the pull request explaining why you believe the failure is unrelated. A maintainer will re-run the status check for you. If we conclude that the failure was a false positive, then we will open an issue to track that problem with our status check suite.</details>

While the prerequisites above must be satisfied prior to having your pull request reviewed, the reviewer(s) may ask you to complete additional design work, tests, or other changes before your pull request can be ultimately accepted.

## Styleguides

### Git Commit Messages

* Limit the first line to 80 characters or less.
* Add a concise description of what your change does.
* Reference issues and pull requests liberally after the first line.

### Documentation Styleguide

* Use [Markdown](https://daringfireball.net/projects/markdown).

## Additional Notes

### Issue and Pull Request Labels

This section lists the labels we use to help us track and manage issues and pull requests.

[GitHub search](https://help.github.com/articles/searching-issues/) makes it easy to use labels for finding groups of issues or pull requests you're interested in. We  encourage you to read about [other search filters](https://help.github.com/articles/searching-issues/) which will help you write more focused queries.

The labels are loosely grouped by their purpose, but it's not required that every issue have a label from every group or that an issue can't have more than one label from the same group.

Please open an issue on `etr/libhttpserver` if you have suggestions for new labels.

#### Type of Issue and Issue State

| Label name | `etr/libhttpserver` :mag_right: | Description |
| --- | --- | --- |
| `feature-request` | [search][search-libhttpserver-repo-label-feature-request] | Feature requests or enhancements. |
| `bug` | [search][search-libhttpserver-repo-label-bug] | Confirmed bugs or reports that are very likely to be bugs. |
| `question` | [search][search-libhttpserver-repo-label-question] | Questions more than bug reports or feature requests (e.g. how do I do X). |
| `feedback` | [search][search-libhttpserver-repo-label-feedback] | General feedback more than bug reports or feature requests. |
| `help-wanted` | [search][search-libhttpserver-repo-label-help-wanted] | The maintainer would appreciate help from the community in resolving these issues. |
| `beginner` | [search][search-libhttpserver-repo-label-beginner] | Less complex issues which would be good first issues to work on for users who want to contribute to libhttpserver. |
| `more-information-needed` | [search][search-libhttpserver-repo-label-more-information-needed] | More information needs to be collected about these problems or feature requests (e.g. steps to reproduce). |
| `needs-reproduction` | [search][search-libhttpserver-repo-label-needs-reproduction] | Likely bugs, but haven't been reliably reproduced. |
| `blocked` | [search][search-libhttpserver-repo-label-blocked] | Issues blocked on other issues. |
| `duplicate` | [search][search-libhttpserver-repo-label-duplicate] | Issues which are duplicates of other issues, i.e. they have been reported before. |
| `wontfix` | [search][search-libhttpserver-repo-label-wontfix] | The maintainers have decided not to fix these issues for now, either because they're working as intended or for some other reason. |
| `invalid` | [search][search-libhttpserver-repo-label-invalid] | Issues which aren't valid (e.g. user errors). |

#### Topic Categories

| Label name | `etr/libhttpserver` :mag_right: | Description |
| --- | --- | --- |
| `windows` | [search][search-libhttpserver-repo-label-windows] | Related to  Windows. |
| `linux` | [search][search-libhttpserver-repo-label-linux] | Related to  Linux. |
| `mac` | [search][search-libhttpserver-repo-label-mac] | Related to  macOS. |
| `travis` | [search][search-libhttpserver-repo-label-travis] | Related to  travis and CI in general. |
| `tests` | [search][search-libhttpserver-repo-label-tests] | Related to tests (add tests, fix tests, etc...). |
| `documentation` | [search][search-libhttpserver-repo-label-documentation] | Related to any type of documentation. |
| `performance` | [search][search-libhttpserver-repo-label-performance] | Related to performance. |
| `security` | [search][search-libhttpserver-repo-label-security] | Related to security. |
| `api` | [search][search-libhttpserver-repo-label-api] | Related to libhttpserver's public APIs. |
| `git` | [search][search-libhttpserver-repo-label-git] | Related to Git functionality (e.g. problems with gitignore files or with showing the correct file status). |

#### Pull Request Labels

| Label name | `etr/libhttpserver` :mag_right: | Description |
| --- | --- | --- |
| `work-in-progress` | [search][search-libhttpserver-repo-label-work-in-progress] | Pull requests which are still being worked on, more changes will follow. |
| `needs-review` | [search][search-libhttpserver-repo-label-needs-review] | Pull requests which need code review, and approval from maintainers. |
| `under-review` | [search][search-libhttpserver-repo-label-under-review] | Pull requests being reviewed by maintainers. |
| `requires-changes` | [search][search-libhttpserver-repo-label-requires-changes] | Pull requests which need to be updated based on review comments and then reviewed again. |
| `needs-testing` | [search][search-libhttpserver-repo-label-needs-testing] | Pull requests which need manual testing. |

[search-libhttpserver-repo-label-feature-request]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Afeature-request
[search-libhttpserver-repo-label-bug]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Abug
[search-libhttpserver-repo-label-question]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Aquestion
[search-libhttpserver-repo-label-feedback]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Afeedback
[search-libhttpserver-repo-label-help-wanted]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Ahelp-wanted
[search-libhttpserver-repo-label-beginner]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Abeginner
[search-libhttpserver-repo-label-more-information-needed]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Amore-information-needed
[search-libhttpserver-repo-label-needs-reproduction]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Aneeds-reproduction
[search-libhttpserver-repo-label-triage-help-needed]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Atriage-help-needed
[search-libhttpserver-repo-label-windows]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Awindows
[search-libhttpserver-repo-label-linux]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Alinux
[search-libhttpserver-repo-label-mac]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Amac
[search-libhttpserver-repo-label-travis]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Atravis
[search-libhttpserver-repo-label-tests]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Atests
[search-libhttpserver-repo-label-documentation]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Adocumentation
[search-libhttpserver-repo-label-performance]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Aperformance
[search-libhttpserver-repo-label-security]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Asecurity
[search-libhttpserver-repo-label-api]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Aapi
[search-libhttpserver-repo-label-git]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Agit
[search-libhttpserver-repo-label-blocked]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Ablocked
[search-libhttpserver-repo-label-duplicate]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Aduplicate
[search-libhttpserver-repo-label-wontfix]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Awontfix
[search-libhttpserver-repo-label-invalid]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Ainvalid
[search-libhttpserver-repo-label-build-error]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Abuild-error
[search-libhttpserver-repo-label-installer]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Ainstaller
[search-libhttpserver-repo-label-deprecation-help]: https://github.com/search?q=is%3Aopen+is%3Aissue+repo%3Aetr%23Flibhttpserver+label%3Adeprecation-help
[search-libhttpserver-repo-label-work-in-progress]: https://github.com/search?q=is%3Aopen+is%3Apr+repo%3Aetr%23Flibhttpserver+label%3Awork-in-progress
[search-libhttpserver-repo-label-needs-review]: https://github.com/search?q=is%3Aopen+is%3Apr+repo%3Aetr%23Flibhttpserver+label%3Aneeds-review
[search-libhttpserver-repo-label-under-review]: https://github.com/search?q=is%3Aopen+is%3Apr+repo%3Aetr%23Flibhttpserver+label%3Aunder-review
[search-libhttpserver-repo-label-requires-changes]: https://github.com/search?q=is%3Aopen+is%3Apr+repo%3Aetr%23Flibhttpserver+label%3Arequires-changes
[search-libhttpserver-repo-label-needs-testing]: https://github.com/search?q=is%3Aopen+is%3Apr+repo%3Aetr%23Flibhttpserver+label%3Aneeds-testing

[beginner]:https://github.com/search?utf8=%E2%9C%93&q=is%3Aopen+is%3Aissue+label%3Abeginner+label%3Ahelp-wanted+user%3Aetr+sort%3Acomments-desc
[help-wanted]:https://github.com/search?q=is%3Aopen+is%3Aissue+label%3Ahelp-wanted+user%3Aetr+sort%3Acomments-desc+-label%3Abeginner
