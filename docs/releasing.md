# Releasing asmir

For maintainers. A release is a tag on GitHub plus an update to the AUR
package that builds from it.

1. **Write the release notes** in a file outside the repository, or one you
   don't commit: what changed for people using asmir, and anything they need
   to do after updating.
2. **Run the release** from an up-to-date `main` with nothing uncommitted:

   ```sh
   bin/release 0.2.0 ~/notes.md
   ```

   It runs the tests, then asks before publishing anything. After that it:

   - bumps the version in `src/main.cpp` and `pkgbuild/PKGBUILD` and commits
     "Release 0.2.0"
   - tags `v0.2.0`, pushes, and creates the GitHub release with your notes
   - fills in the checksum of GitHub's tarball and commits "Check the 0.2.0
     tarball"
   - builds the package from that tarball, tests included
   - commits the new `PKGBUILD` and `.SRCINFO` in `../asmir-aur`, the AUR
     package's own repository, if you have it checked out

3. **Install it** yourself with the `pacman -U` command it prints, and try a
   dictation.
4. **Publish to the AUR** with `git push` in `../asmir-aur`
   (`ssh://aur@aur.archlinux.org/asmir.git`).

If whisper.cpp moves to a new version, update the submodule, `_whisper` in
the `PKGBUILD`, and its checksum together, and run `bin/test` first.
