# Releasing asmir

For maintainers. A release is a tag on GitHub plus an update to the AUR
package that builds from it.

1. **Bump the version** in `src/main.cpp` (`Version`) and `pkgbuild/PKGBUILD`
   (`pkgver`, and `pkgrel=1`). Commit: "Release 0.2.0".
2. **Tag and publish** on GitHub:

   ```sh
   git tag v0.2.0 && git push origin main v0.2.0
   gh release create v0.2.0 --title "asmir 0.2.0" --notes-file notes.md
   ```

3. **Fill in the checksum** of GitHub's tarball for the new tag:

   ```sh
   cd pkgbuild && updpkgsums && cd ..
   ```

   Commit the updated `PKGBUILD` ("Check the 0.2.0 tarball").
4. **Try the package** before anyone else does:

   ```sh
   cd pkgbuild && makepkg -fsi && cd ..
   ```

5. **Update the AUR.** The AUR package lives in its own git repository
   (`ssh://aur@aur.archlinux.org/asmir.git`) holding just `PKGBUILD` and
   `.SRCINFO`:

   ```sh
   cp pkgbuild/PKGBUILD ../asmir-aur/
   cd ../asmir-aur
   makepkg --printsrcinfo > .SRCINFO
   git commit -am "Update to 0.2.0" && git push
   ```

If whisper.cpp moves to a new version, update the submodule, `_whisper` in
the `PKGBUILD`, and its checksum together, and run `bin/test` first.
