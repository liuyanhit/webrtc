package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path"
	"runtime"
	"strings"
	"time"
)

var ldlinuxpath = "/lib64/ld-linux-x86-64.so.2"

func otoolL(lib string) (paths []string, err error) {
	c := exec.Command("otool", "-L", lib)
	stdout, _ := c.StdoutPipe()
	br := bufio.NewReader(stdout)
	if err = c.Start(); err != nil {
		err = fmt.Errorf("otoolL: %s", err)
		return
	}
	for i := 0; ; i++ {
		var line string
		var rerr error
		if line, rerr = br.ReadString('\n'); rerr != nil {
			break
		}
		if i == 0 {
			continue
		}
		f := strings.Fields(line)
		if len(f) >= 2 && strings.HasPrefix(f[1], "(") {
			paths = append(paths, f[0])
		}
	}
	return
}

func installNameTool(lib string, change [][]string) error {
	if len(change) == 0 {
		return nil
	}
	args := []string{}
	for _, c := range change {
		args = append(args, "-change")
		args = append(args, c[0])
		args = append(args, c[1])
	}
	args = append(args, lib)
	c := exec.Command("install_name_tool", args...)

	if err := c.Run(); err != nil {
		return fmt.Errorf("install_name_tool(%v): %s", change, err)
	}
	return nil
}

type CopyEntry struct {
	Realpath string
	IsBin    bool
}

func packlibDarwin(copy []CopyEntry) error {
	visited := map[string]bool{}
	isbin := map[string]bool{}

	var dfs func(k string) error
	dfs = func(k string) error {
		if visited[k] {
			return nil
		}
		visited[k] = true
		paths, err := otoolL(k)
		if err != nil {
			return err
		}
		for _, p := range paths {
			if strings.HasPrefix(p, "@") {
				const rpath = "@rpath/"
				if strings.HasPrefix(p, rpath) {
					lp := locate(strings.TrimPrefix(p, rpath))
					if lp != "" {
						p = lp
					} else {
						continue
					}
				} else {
					continue
				}
			}
			if strings.HasPrefix(p, "/usr/lib") {
				continue
			}
			if strings.HasPrefix(p, "/System") {
				continue
			}
			if err := dfs(p); err != nil {
				return err
			}
		}
		return nil
	}

	for _, f := range copy {
		if f.IsBin {
			isbin[f.Realpath] = true
		}
		if err := dfs(f.Realpath); err != nil {
			return err
		}
	}

	change := [][]string{}
	for p := range visited {
		if !strings.HasPrefix(p, "/") {
			continue
		}
		fname := path.Join("lib", path.Base(p))
		change = append(change, []string{p, fname})
	}

	for p := range visited {
		dstdir := "lib"
		if isbin[p] {
			dstdir = "bin"
		}
		fname := path.Join(dstdir, path.Base(p))
		c := exec.Command("cp", "-f", p, fname)
		if err := c.Run(); err != nil {
			return fmt.Errorf("cp %s failed: %s", p, err)
		}
		c = exec.Command("chmod", "744", fname)
		if err := c.Run(); err != nil {
			return fmt.Errorf("chmod %s failed: %s", fname, err)
		}
		if err := installNameTool(fname, change); err != nil {
			return fmt.Errorf("change %s failed: %s", fname, err)
		}
		fmt.Println("copy", fname)
	}

	return nil
}

var libsearchpath []string

func locate(name string) (out string) {
	for _, root := range libsearchpath {
		p := path.Join(root, name)
		_, serr := os.Stat(p)
		if serr == nil {
			out = p
			return
		}
	}
	return
}

type Entry struct {
	Name     string
	Realpath string
	IsBin    bool
}

func ldd(lib string) (paths []Entry, err error) {
	c := exec.Command("ldd", lib)
	stdout, _ := c.StdoutPipe()
	if err = c.Start(); err != nil {
		err = fmt.Errorf("ldd: %s", err)
		return
	}
	br := bufio.NewReader(stdout)
	for {
		line, rerr := br.ReadString('\n')
		if rerr != nil {
			break
		}
		f := strings.Fields(line)
		if len(f) < 3 {
			continue
		}
		if strings.HasSuffix(f[0], ":") {
			continue
		}
		name := f[0]
		if name == "" {
			continue
		}
		if name == "linux-vdso.so.1" {
			continue
		}
		if name == ldlinuxpath {
			continue
		}
		var realpath string
		if strings.HasPrefix(f[2], "/") {
			realpath = f[2]
		} else {
			realpath = locate(name)
		}
		paths = append(paths, Entry{Name: name, Realpath: realpath})
	}
	return
}

func packlibLinux(copy []CopyEntry) error {
	visited := map[string]Entry{}

	var dfs func(e Entry) error
	dfs = func(e Entry) (err error) {
		if _, ok := visited[e.Name]; ok {
			return
		}
		visited[e.Name] = e
		var paths []Entry
		if paths, err = ldd(e.Realpath); err != nil {
			return
		}
		for _, p := range paths {
			if err = dfs(p); err != nil {
				return
			}
		}
		return
	}

	for _, f := range copy {
		dfs(Entry{Name: path.Base(f.Realpath), Realpath: f.Realpath, IsBin: f.IsBin})
	}

	for _, e := range visited {
		if e.Realpath == "" {
			continue
		}
		src := e.Realpath
		dstdir := "lib"
		if e.IsBin {
			dstdir = "bin"
		}
		dst := path.Join(dstdir, e.Name)
		c := exec.Command("cp", "-f", src, dst)
		if err := c.Run(); err != nil {
			err = fmt.Errorf("cp %s %s: %s", src, dst, err)
			return err
		}
		fmt.Println(src, dst)
	}

	c := exec.Command("cp", "-f", ldlinuxpath, path.Join("lib", "ld-linux.so"))
	if err := c.Run(); err != nil {
		return err
	}

	return nil
}

func runPack(copy []CopyEntry) error {
	os.RemoveAll("lib")
	os.RemoveAll("bin")
	os.Mkdir("lib", 0744)
	os.Mkdir("bin", 0744)
	switch runtime.GOOS {
	case "darwin":
		if err := packlibDarwin(copy); err != nil {
			return err
		}
	case "linux":
		if err := packlibLinux(copy); err != nil {
			return err
		}
	}
	return nil
}

func runUpload(name string) error {
	var c *exec.Cmd
	uploadname := fmt.Sprintf("%s-%s.tar.bz2", name, runtime.GOOS)
	tarname := fmt.Sprintf("/tmp/%d", time.Now().UnixNano())
	defer os.Remove(tarname)

	c = exec.Command("tar", "cjf", tarname, "bin", "lib")
	if err := c.Run(); err != nil {
		return err
	}

	c = exec.Command("qup", tarname, uploadname)
	c.Stdout = os.Stdout
	c.Stderr = os.Stderr
	if err := c.Run(); err != nil {
		return err
	}

	return nil
}

func runcmd(path string, args ...string) error {
	c := exec.Command(path, args...)
	c.Stdout = os.Stdout
	c.Stderr = os.Stderr
	return c.Run()
}

func run() error {
	upload := flag.Bool("u", false, "upload")
	search := flag.String("s", "", "lib search path")
	bin := flag.String("bin", "", "bin files")
	lib := flag.String("lib", "", "lib files")
	name := flag.String("n", "", "name")
	flag.Parse()

	if *name == "" {
		return fmt.Errorf("name is empty")
	}

	if *search != "" {
		libsearchpath = strings.Split(*search, ";")
	}

	copy := []CopyEntry{}
	if *bin != "" {
		for _, f := range strings.Split(*bin, ";") {
			copy = append(copy, CopyEntry{IsBin: true, Realpath: f})
		}
	}
	if *lib != "" {
		for _, f := range strings.Split(*lib, ";") {
			copy = append(copy, CopyEntry{Realpath: f})
		}
	}

	if len(copy) == 0 {
		return fmt.Errorf("specifiy some lib or bin files")
	}

	if err := runPack(copy); err != nil {
		return err
	}

	if *upload {
		if err := runUpload(*name); err != nil {
			return err
		}
	}

	return nil
}

func main() {
	if err := run(); err != nil {
		fmt.Println(err)
	}
}
