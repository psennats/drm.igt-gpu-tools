CONTRIBUTING
============

Patches to igt-gpu-tools are very much welcome, we really want this to be the
universal set of low-level tools and testcases for kernel graphics drivers
on Linux and similar platforms. So please bring on porting patches, bugfixes,
improvements for documentation and new tools and testcases.


The Code
--------

- The code should follow kernel coding style:
  https://www.kernel.org/doc/html/latest/process/coding-style.html

- Testcases (subtests) have to use minus signs (-) as a word separator.
  The generated documentation contains glossary of commonly used terms.

- All new test have to be described using `igt_describe()` family of
  functions. The description should contain the spirit of the test (what is
  the general idea behind the test) and *not* the letter (C to English
  translation of the test). Refer to [`igt_describe()`
  documentation][igt-describe] for more details.

- The generated documentation contains explanation of magic control blocks like
  `igt_subtest` and `igt_fixture`. Please make sure that you understand their
  roles and limitation before using/altering them.

- Also please make full use of all the helpers and convenience macros
  provided by the igt library. The semantic patch lib/igt.cocci can help with
  more automatic conversions.

- Tests that use kernel interfaces (uapi, sysfs, or even debugfs) that
  become deprecated in favour of new interfaces should have fallbacks
  to the deprecated interfaces if the new stuff is not present in the
  running kernel. The same IGT version can be used to test the tip of
  development along with stable kernel releases that way.

[igt-describe]: https://drm.pages.freedesktop.org/igt-gpu-tools/igt-gpu-tools-Core.html#igt-describe

IGT libraries
-------------
- Tests and benchmarks are the main usage of IGT libraries, so they
  could use test specific macros/functions, for example igt_assert,
  igt_require, igt_skip, igt_info or igt_debug.

- New library function could be written when it will have at least two
  different users, for example if it could be used by two or more tests.
  In some cases single user can be accepted, when it is very likely it
  will be used in future.

- In a new library function():
  if it uses some of the macros igt_assert/igt_require/igt_skip then
  consider to write also __function() with the same functionality but
  without those macros.

- Libraries and igt_runner
  Runner should not use lib functions. It is crucial for CI runs so using
  libraries puts a risk of bringing changes meant for tests which in turn
  could break runner.
  Note: You will find places where igt_runner uses lib functions - this will
  be on ToDo list to be fixed.

- Libraries and tools/
  Give some thought if you are planning to use IGT lib code in tools, some
  IGT lib functions might not be appropriate in tools. For example, any
  abnormal condition should be simply reported by printf or fprintf to
  stdout/stderr and then tool should exit gracefully.

Sending Patches
---------------

- igt-gpu-tools is MIT licensed and we require contributions to follow the
  developer's certificate of origin: http://developercertificate.org/

- Please submit patches formatted with git send-email/git format-patch or
  equivalent to:

      Development mailing list for IGT GPU Tools <igt-dev@lists.freedesktop.org>

  For patches affecting the driver directly please "cc" the appropriate driver
  mailing list and make sure you are using:

      --subject-prefix="PATCH i-g-t"

  so IGT patches are easily identified in the massive amount mails on driver's
  mailing list. To ensure this is always done, meson.sh (and autogen.sh) will
  run:

      git config format.subjectprefix "PATCH i-g-t"

  on its first invocation.

- If you plan to contribute regularly, please subscribe to igt-dev mailinglist:
  https://lists.freedesktop.org/mailman/listinfo/igt-dev
  When you are not subscribed, please note that your contribution will take
  more time to reach to mailing list. You could find out if it was delivered or
  what is a testing status of your patches at page:
  https://patchwork.freedesktop.org/project/igt/series/
  and also on
  https://lore.kernel.org/igt-dev/

- Place relevant prefix in subject, for example when your change is in one
  testfile, use its name without '.c' nor '.h' suffix, like:
  tests/simple_test: short description
  Consider sending cover letter with your patch, so if you decide to change
  subject it can still be linked into same patchseries on patchwork.

- Look into some guides from Linux and Open Source community:
  https://kernelnewbies.org/PatchPhilosophy
  https://www.kernel.org/doc/html/latest/process/submitting-patches.html
  https://www.kernel.org/doc/html/latest/process/submit-checklist.html

- Patches need to be reviewed on the mailing list. Exceptions only apply for
  testcases and tooling for drivers with just a single contributor (e.g. vc4).
  In this case patches must still be submitted to the mailing list first.
  Testcase should preferably be cross-reviewed by the same people who write and
  review the kernel feature itself.

- When patches from new contributors (without commit access) are stuck, for
  anything related to the regular releases, issues with packaging and
  integrating platform support or any other igt-gpu-tools issues, please
  contact one of the maintainers (listed in the MAINTAINERS file) and cc the
  igt-dev mailing list.

- Before sending use Linux kernel script 'checkpatch.pl' for checking your
  patchset. You could ignore some of them like 'line too long' or 'typedef'
  but most of the time its log is accurate. Useful options you could use:
  --emacs --strict --show-types --max-line-length=100 \
  --ignore=BIT_MACRO,SPLIT_STRING,LONG_LINE_STRING,BOOL_MEMBER

- Changes to the testcases are automatically tested. Take the results into
  account before merging.  Please also reply to CI failures if you think they
  are unrelated, add also to Cc CI e-mail which is present in message.  This
  can help our bug-filing team. When replying, you can cut a message after
  'Known bugs' to keep it in reasonable size.


Commit Rights
-------------

Commit rights will be granted to anyone who requests them and fulfills the
below criteria:

- Submitted a few (5-10 as a rule of thumb) non-trivial (not just simple
  spelling fixes and whitespace adjustment) patches that have been merged
  already.

- Are actively participating on discussions about their work (on the mailing
  list or IRC). This should not be interpreted as a requirement to review other
  peoples patches but just make sure that patch submission isn't one-way
  communication. Cross-review is still highly encouraged.

- Will be regularly contributing further patches. This includes regular
  contributors to other parts of the open source graphics stack who only
  do the oddball rare patch within igt itself.

- Agrees to use their commit rights in accordance with the documented merge
  criteria, tools, and processes.

Create a gitlab account at https://gitlab.freedesktop.org/ and apply
for access to the IGT gitlab project,
http://gitlab.freedesktop.org/drm/igt-gpu-tools and please ping the
maintainers if your request is stuck.

Committers are encouraged to request their commit rights get removed when they
no longer contribute to the project. Commit rights will be reinstated when they
come back to the project.

Maintainers and committers should encourage contributors to request commit
rights, especially junior contributors tend to underestimate their skills.


Code of Conduct
---------------

Please be aware the fd.o Code of Conduct also applies to igt:

https://www.freedesktop.org/wiki/CodeOfConduct/

See the MAINTAINERS file for contact details of the igt maintainers.

Abuse of commit rights, like engaging in commit fights or willfully pushing
patches that violate the documented merge criteria, will also be handled through
the Code of Conduct enforcement process.

Happy hacking!
