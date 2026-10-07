# GitHub push status

Local commits ready on `master`:

- ace314b Docs: Milliken G1-G18 closed
- 31f598d Full tree sources network lobby UI input FFB
- e122b3c CMake and documentation
- aca68de Milliken G1-G18 core physics and tools

Remote: `https://github.com/SimonTek27/ksengine.git`

Push blocked: no GitHub credentials in this environment.

To publish from this tree:

```bash
cd artifacts
git remote set-url origin https://<TOKEN>@github.com/SimonTek27/ksengine.git
# or: git remote set-url origin git@github.com:SimonTek27/ksengine.git
git push -u origin master
```

Or connect the GitHub connector and re-request push.
