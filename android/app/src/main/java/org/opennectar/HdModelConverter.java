package org.opennectar;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;

import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

import javax.xml.parsers.DocumentBuilderFactory;

/**
 * Convierte en el dispositivo los rips públicos de Pikmin 3 (Collada + PNG,
 * tal y como se descargan) a los packs NHM1 que carga pc_port/mods/pc_hd_models.cpp.
 * Es un port de tools/build-hd-model-pack.py: mismo formato, mismas decisiones.
 * No redistribuimos nada: el usuario aporta el zip y aquí solo se transforma.
 *
 * Zips reconocidos (por nombre de fichero dentro del archivo):
 *   playerE.dae               → Load/Models/OlimarHD/olimar_hd.nhm
 *   Pikmin/piki_p3_red.dae    → Load/Models/PikminHD/piki_{red,yellow,blue}.nhm + happa_*.nhm
 *   kochappy.dae              → Load/Models/BulborbHD/bulborb_dwarf.nhm
 *   Red Bulborb/model.dae     → Load/Models/BulborbHD/bulborb.nhm
 */
public final class HdModelConverter {
    private HdModelConverter() {}

    private static final String[] JOINTS = {
        "kosinull", "legcentre", "llegjnt", "rlegjnt", "sebonjnt", "headjnt",
        "happajnt1", "happajnt2", "happajnt3", "lhandjnt", "rhandjnt",
    };
    private static final float[] IDENTITY = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

    /** playerE_body es opaca; Pikmin 3 hace el visor traslúcido por shader. */
    private static final int GLASS_ALPHA = 72;
    private static final int CORNEA_ALPHA = 64;
    private static final int PART_FLAG_REPEAT = 1;
    private static final int PART_FLAG_NOCULL = 2;

    /** Resultado de una conversión: cuántos .nhm se escribieron. */
    public static final class Result {
        public final String pack;
        public final int files;
        Result(String pack, int files) { this.pack = pack; this.files = files; }
    }

    // Mismo orden que las filas del submenú HD Models (pc_settings.cpp).
    public static final int KIND_OLIMAR = 0;
    public static final int KIND_PIKMIN = 1;
    public static final int KIND_BULBORB = 2;
    public static final int KIND_DWARF_BULBORB = 3;
    private static final String[] KIND_NAMES = { "Olimar", "Pikmin", "Bulborb", "Dwarf Bulborb" };

    /** Identifica el rip por el .dae que contiene; -1 si no es ninguno conocido. */
    private static int detect(ZipFile zip) {
        if (entry(zip, "playerE.dae") != null) return KIND_OLIMAR;
        if (entry(zip, "piki_p3_red.dae") != null) return KIND_PIKMIN;
        if (entry(zip, "red bulborb/model.dae") != null) return KIND_BULBORB;
        if (entry(zip, "kochappy.dae") != null) return KIND_DWARF_BULBORB;
        return -1;
    }

    /** Devuelve null si el zip no es un rip conocido (entonces se instala como
     *  pack .nhm normal). `expected` es el modelo elegido en el menú (KIND_*)
     *  o -1 para aceptar cualquiera; un rip de otro modelo es un error. */
    public static Result convert(File zipFile, File modelsRoot, int expected) throws Exception {
        try (ZipFile zip = new ZipFile(zipFile)) {
            int kind = detect(zip);
            if (kind < 0) return null;
            if (expected >= 0 && kind != expected) {
                throw new Exception("This zip is the " + KIND_NAMES[kind] + " model, not "
                    + KIND_NAMES[expected] + ".");
            }
            switch (kind) {
                case KIND_OLIMAR:
                    buildOlimar(zip, new File(modelsRoot, "OlimarHD"));
                    return new Result("OlimarHD", 1);
                case KIND_PIKMIN:
                    buildPikmin(zip, new File(modelsRoot, "PikminHD"));
                    return new Result("PikminHD", 6);
                case KIND_BULBORB:
                    buildBulborb(zip, new File(modelsRoot, "BulborbHD"));
                    return new Result("BulborbHD", 1);
                default:
                    buildDwarfBulborb(zip, new File(modelsRoot, "BulborbHD"));
                    return new Result("BulborbHD", 1);
            }
        }
    }

    // ── zip / imagen ───────────────────────────────────────────────────────

    /** Busca una entrada por sufijo (sin distinguir mayúsculas) para tolerar carpetas raíz distintas. */
    private static ZipEntry entry(ZipFile zip, String suffix) {
        String want = suffix.toLowerCase(Locale.ROOT);
        Enumeration<? extends ZipEntry> it = zip.entries();
        while (it.hasMoreElements()) {
            ZipEntry e = it.nextElement();
            String n = e.getName().toLowerCase(Locale.ROOT).replace('\\', '/');
            if (n.equals(want) || n.endsWith("/" + want)) return e;
        }
        return null;
    }

    private static ZipEntry require(ZipFile zip, String suffix) throws Exception {
        ZipEntry e = entry(zip, suffix);
        if (e == null) throw new Exception("The archive is missing " + suffix + ".");
        return e;
    }

    private static Element dae(ZipFile zip, String suffix) throws Exception {
        DocumentBuilderFactory f = DocumentBuilderFactory.newInstance();
        f.setNamespaceAware(true);
        try (InputStream in = zip.getInputStream(require(zip, suffix))) {
            Document doc = f.newDocumentBuilder().parse(in);
            return doc.getDocumentElement();
        }
    }

    private static final class Texture {
        final int width, height;
        final byte[] rgba;
        Texture(int w, int h, byte[] p) { width = w; height = h; rgba = p; }

        Texture translucent(int alpha) {
            byte[] copy = rgba.clone();
            for (int i = 3; i < copy.length; i += 4) copy[i] = (byte) alpha;
            return new Texture(width, height, copy);
        }
    }

    private static Texture rgba(ZipFile zip, String suffix) throws Exception {
        BitmapFactory.Options opts = new BitmapFactory.Options();
        opts.inScaled = false;
        opts.inPremultiplied = false;
        opts.inPreferredConfig = Bitmap.Config.ARGB_8888;
        Bitmap bmp;
        try (InputStream in = zip.getInputStream(require(zip, suffix))) {
            bmp = BitmapFactory.decodeStream(in, null, opts);
        }
        if (bmp == null) throw new Exception("Could not decode " + suffix + ".");
        int w = bmp.getWidth(), h = bmp.getHeight();
        int[] argb = new int[w * h];
        bmp.getPixels(argb, 0, w, 0, 0, w, h);
        bmp.recycle();
        byte[] out = new byte[w * h * 4];
        for (int i = 0, o = 0; i < argb.length; i++, o += 4) {
            int p = argb[i];
            out[o] = (byte) (p >> 16);
            out[o + 1] = (byte) (p >> 8);
            out[o + 2] = (byte) p;
            out[o + 3] = (byte) (p >>> 24);
        }
        return new Texture(w, h, out);
    }

    // ── DOM ────────────────────────────────────────────────────────────────

    private static List<Element> children(Element e, String name) {
        List<Element> out = new ArrayList<>();
        if (e == null) return out;
        for (Node n = e.getFirstChild(); n != null; n = n.getNextSibling()) {
            if (n.getNodeType() == Node.ELEMENT_NODE && name.equals(n.getLocalName())) out.add((Element) n);
        }
        return out;
    }

    private static Element child(Element e, String... path) {
        Element cur = e;
        for (String name : path) {
            List<Element> c = children(cur, name);
            if (c.isEmpty()) return null;
            cur = c.get(0);
        }
        return cur;
    }

    private static List<Element> descendants(Element e, String name) {
        List<Element> out = new ArrayList<>();
        NodeList list = e.getElementsByTagNameNS("*", name);
        for (int i = 0; i < list.getLength(); i++) out.add((Element) list.item(i));
        return out;
    }

    private static Element input(Element parent, String semantic) {
        for (Element in : children(parent, "input")) {
            if (semantic.equals(in.getAttribute("semantic"))) return in;
        }
        return null;
    }

    private static String[] tokens(Element e) {
        String t = e == null ? null : e.getTextContent();
        if (t == null) return new String[0];
        t = t.trim();
        return t.isEmpty() ? new String[0] : t.split("\\s+");
    }

    private static float[] floats(Element e) {
        String[] tk = tokens(e);
        float[] out = new float[tk.length];
        for (int i = 0; i < tk.length; i++) out[i] = Float.parseFloat(tk[i]);
        return out;
    }

    private static int[] ints(Element e) {
        String[] tk = tokens(e);
        int[] out = new int[tk.length];
        for (int i = 0; i < tk.length; i++) out[i] = Integer.parseInt(tk[i]);
        return out;
    }

    private static String ref(Element e, String attr) {
        String v = e.getAttribute(attr);
        return v.startsWith("#") ? v.substring(1) : v;
    }

    private static final class Source {
        float[] floats;
        String[] names;
        int stride;
    }

    private static Map<String, Source> sourceData(Element parent) {
        Map<String, Source> out = new HashMap<>();
        for (Element source : children(parent, "source")) {
            Source s = new Source();
            Element acc = child(source, "technique_common", "accessor");
            String stride = acc == null ? "" : acc.getAttribute("stride");
            s.stride = stride.isEmpty() ? 1 : Integer.parseInt(stride);
            Element arr = child(source, "float_array");
            if (arr != null) s.floats = floats(arr);
            else s.names = tokens(child(source, "Name_array"));
            out.put(source.getAttribute("id"), s);
        }
        return out;
    }

    // ── skins ──────────────────────────────────────────────────────────────

    private static final class Influence {
        final String[] joints;
        final float[] weights;
        Influence(String[] j, float[] w) { joints = j; weights = w; }
    }

    private static final class Skins {
        final Map<String, Influence[]> controllers = new HashMap<>();
        final LinkedHashMap<String, float[]> bindByJoint = new LinkedHashMap<>();
        String[] jointNames() { return bindByJoint.keySet().toArray(new String[0]); }
    }

    private static Skins readSkins(Element root) throws Exception {
        Skins out = new Skins();
        Element lib = child(root, "library_controllers");
        for (Element controller : children(lib, "controller")) {
            Element skin = child(controller, "skin");
            Map<String, Source> data = sourceData(skin);
            Element joints = child(skin, "joints");
            String[] names = data.get(ref(input(joints, "JOINT"), "source")).names;
            float[] binds = data.get(ref(input(joints, "INV_BIND_MATRIX"), "source")).floats;
            for (int i = 0; i < names.length; i++) {
                if (!out.bindByJoint.containsKey(names[i])) {
                    out.bindByJoint.put(names[i], Arrays.copyOfRange(binds, i * 16, i * 16 + 16));
                }
            }
            Element vw = child(skin, "vertex_weights");
            Element weightInput = input(vw, "WEIGHT");
            float[] weights = data.get(ref(weightInput, "source")).floats;
            int jointOffset = Integer.parseInt(input(vw, "JOINT").getAttribute("offset"));
            int weightOffset = Integer.parseInt(weightInput.getAttribute("offset"));
            int stride = 0;
            for (Element in : children(vw, "input")) stride = Math.max(stride, Integer.parseInt(in.getAttribute("offset")));
            stride += 1;
            int[] vcount = ints(child(vw, "vcount"));
            int[] v = ints(child(vw, "v"));
            Influence[] influences = new Influence[vcount.length];
            int cursor = 0;
            for (int vi = 0; vi < vcount.length; vi++) {
                int count = vcount[vi];
                String[] rj = new String[count];
                float[] rw = new float[count];
                for (int k = 0; k < count; k++) {
                    rj[k] = names[v[cursor + jointOffset]];
                    rw[k] = weights[v[cursor + weightOffset]];
                    cursor += stride;
                }
                // Orden descendente por peso (estable), recorte a 4 y renormalizado.
                Integer[] order = new Integer[count];
                for (int k = 0; k < count; k++) order[k] = k;
                Arrays.sort(order, (a, b) -> Float.compare(rw[b], rw[a]));
                int keep = Math.min(4, count);
                String[] j = new String[keep];
                float[] w = new float[keep];
                float total = 0;
                for (int k = 0; k < keep; k++) { j[k] = rj[order[k]]; w[k] = rw[order[k]]; total += w[k]; }
                if (total == 0) total = 1;
                for (int k = 0; k < keep; k++) w[k] /= total;
                influences[vi] = new Influence(j, w);
            }
            out.controllers.put(ref(skin, "source"), influences);
        }
        return out;
    }

    // ── triángulos ─────────────────────────────────────────────────────────

    private interface Wrap { float apply(float t); }
    private interface Transform { float[] apply(float[] v); }

    private static final Wrap CLAMP = t -> t - (float) Math.floor(t);
    private static final Wrap REPEAT = t -> t;
    private static final Wrap MIRROR = t -> {
        float m = t - 2f * (float) Math.floor(t / 2f);
        return m > 1f ? 2f - m : m;
    };

    private static String material(Element geometry) {
        Element tri = child(geometry, "mesh", "triangles");
        return tri == null ? "" : tri.getAttribute("material");
    }

    /** Vértices planos: 3 pos, 3 nrm, 2 uv, 4 hueso, 4 peso (16 floats). */
    private static List<float[]> expand(Element geometry, Skins skins, String[] jointNames, Wrap wrap,
                                        Transform transform) throws Exception {
        Element mesh = child(geometry, "mesh");
        Map<String, Source> data = sourceData(mesh);
        Element triangles = child(mesh, "triangles");
        if (triangles == null) throw new Exception("Geometry without <triangles>.");
        Element verticesNode = child(mesh, "vertices");
        Map<String, Integer> offsets = new HashMap<>();
        Map<String, String> sources = new HashMap<>();
        int stride = 0;
        for (Element in : children(triangles, "input")) {
            int offset = Integer.parseInt(in.getAttribute("offset"));
            stride = Math.max(stride, offset);
            String semantic = in.getAttribute("semantic");
            if ("VERTEX".equals(semantic)) {
                for (Element sub : children(verticesNode, "input")) {
                    offsets.put(sub.getAttribute("semantic"), offset);
                    sources.put(sub.getAttribute("semantic"), ref(sub, "source"));
                }
            } else {
                offsets.put(semantic, offset);
                sources.put(semantic, ref(in, "source"));
            }
        }
        stride += 1;
        int[] indices = ints(child(triangles, "p"));
        Influence[] influences = skins.controllers.get(geometry.getAttribute("id"));
        if (influences == null) throw new Exception("No skin for geometry " + geometry.getAttribute("id") + ".");
        Map<String, Integer> jointIndex = new HashMap<>();
        for (int i = 0; i < jointNames.length; i++) jointIndex.put(jointNames[i], i);

        List<float[]> out = new ArrayList<>(indices.length / stride);
        for (int base = 0; base < indices.length; base += stride) {
            float[] pos = value(data, sources, offsets, indices, base, "POSITION", 3);
            float[] nrm = value(data, sources, offsets, indices, base, "NORMAL", 3);
            float[] uv = value(data, sources, offsets, indices, base, "TEXCOORD", 2);
            if (transform != null) { pos = transform.apply(pos); nrm = transform.apply(nrm); }
            Influence skin = influences[indices[base + offsets.get("POSITION")]];
            float[] vtx = new float[16];
            vtx[0] = pos[0]; vtx[1] = pos[1]; vtx[2] = pos[2];
            vtx[3] = nrm[0]; vtx[4] = nrm[1]; vtx[5] = nrm[2];
            // Collada tiene el origen UV abajo-izquierda; las imágenes arriba-izquierda.
            vtx[6] = wrap.apply(uv[0]);
            vtx[7] = wrap.apply(1f - uv[1]);
            for (int k = 0; k < skin.joints.length; k++) {
                Integer idx = jointIndex.get(skin.joints[k]);
                if (idx == null) throw new Exception("Unknown joint " + skin.joints[k] + ".");
                vtx[8 + k] = idx;
                vtx[12 + k] = skin.weights[k];
            }
            out.add(vtx);
        }
        return out;
    }

    private static float[] value(Map<String, Source> data, Map<String, String> sources, Map<String, Integer> offsets,
                                 int[] indices, int base, String semantic, int size) throws Exception {
        String src = sources.get(semantic);
        Integer off = offsets.get(semantic);
        if (src == null || off == null) throw new Exception("Mesh has no " + semantic + " input.");
        Source s = data.get(src);
        int index = indices[base + off];
        return Arrays.copyOfRange(s.floats, index * s.stride, index * s.stride + size);
    }

    // ── escritura NHM ──────────────────────────────────────────────────────

    private static final class Part {
        final List<float[]> vertices;
        final Texture texture;
        final int flags;
        Part(List<float[]> v, Texture t, int f) { vertices = v; texture = t; flags = f; }
        Part(List<float[]> v, Texture t) { this(v, t, 0); }
    }

    private static final class Bone {
        final String name;
        final float[] inverseBind;
        Bone(String n, float[] m) { name = n; inverseBind = m; }
    }

    private static void u32(ByteArrayOutputStream o, int v) {
        o.write(v); o.write(v >> 8); o.write(v >> 16); o.write(v >> 24);
    }

    private static void f32(ByteArrayOutputStream o, float v) { u32(o, Float.floatToIntBits(v)); }

    private static void writePack(File output, List<Bone> bones, List<Part> parts) throws Exception {
        boolean anyFlags = false;
        for (Part p : parts) anyFlags |= p.flags != 0;
        int version = anyFlags ? 2 : 1;
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        o.write('N'); o.write('H'); o.write('M'); o.write('1');
        u32(o, version);
        u32(o, bones.size());
        u32(o, parts.size());
        for (Bone b : bones) {
            byte[] name = b.name.getBytes("US-ASCII");
            o.write(name.length);
            o.write(name);
            for (float f : b.inverseBind) f32(o, f);
        }
        for (Part p : parts) {
            u32(o, p.vertices.size());
            u32(o, p.texture.width);
            u32(o, p.texture.height);
            u32(o, p.texture.rgba.length);
            if (version >= 2) u32(o, p.flags);
            for (float[] v : p.vertices) {
                for (int i = 0; i < 8; i++) f32(o, v[i]);
                for (int i = 8; i < 12; i++) o.write((int) v[i]);
                for (int i = 12; i < 16; i++) f32(o, v[i]);
            }
            o.write(p.texture.rgba);
        }
        File dir = output.getParentFile();
        if (dir != null && !dir.isDirectory()) dir.mkdirs();
        File tmp = new File(output.getPath() + ".part");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            o.writeTo(out);
        }
        if (!tmp.renameTo(output)) {
            output.delete();
            if (!tmp.renameTo(output)) throw new Exception("Could not write " + output.getName() + ".");
        }
    }

    private static List<Bone> bonesFor(String[] joints, Skins skins) {
        List<Bone> out = new ArrayList<>();
        for (String j : joints) {
            float[] m = skins.bindByJoint.get(j);
            out.add(new Bone(j, m == null ? IDENTITY : m));
        }
        return out;
    }

    private static List<Bone> identityBones(String[] joints) {
        List<Bone> out = new ArrayList<>();
        for (String j : joints) out.add(new Bone(j, IDENTITY));
        return out;
    }

    private static List<Element> geometries(Element root) {
        return children(child(root, "library_geometries"), "geometry");
    }

    // ── Olimar ─────────────────────────────────────────────────────────────

    private static void buildOlimar(ZipFile zip, File outDir) throws Exception {
        Element root = dae(zip, "playerE.dae");
        Skins skins = readSkins(root);
        // Solo head_m usa playerE_head; traje, metal, luz y ambas capas del
        // casco (naka interior, soto cristal) usan playerE_body. El cristal va
        // como última parte para dibujarse tras la cara y poder mezclarse.
        List<float[]> body = new ArrayList<>(), head = new ArrayList<>(), glass = new ArrayList<>();
        for (Element g : geometries(root)) {
            String m = material(g);
            List<float[]> target = "head_m".equals(m) ? head
                : ("naka_m".equals(m) || "soto_m".equals(m)) ? glass : body;
            target.addAll(expand(g, skins, JOINTS, CLAMP, null));
        }
        Texture bodyTex = rgba(zip, "playerE_body.png");
        List<Part> parts = new ArrayList<>();
        parts.add(new Part(body, bodyTex));
        parts.add(new Part(head, rgba(zip, "playerE_head.png")));
        parts.add(new Part(glass, bodyTex.translucent(GLASS_ALPHA)));
        writePack(new File(outDir, "olimar_hd.nhm"), bonesFor(JOINTS, skins), parts);
    }

    // ── Pikmin ─────────────────────────────────────────────────────────────

    // Las hojas/capullos/flores de Pikmin 3 llevan el tallo por -Z; Pikmin 1
    // dibuja pikis/happas/*.mod en el espacio de happajnt3 con el tallo por +X.
    private static final Transform HAPPA_TO_JOINT = v -> new float[] { -v[2], v[1], v[0] };

    private static void buildPikmin(ZipFile zip, File outDir) throws Exception {
        String[] colours = { "red", "yellow", "blue" };
        for (String colour : colours) {
            Element root = dae(zip, "piki_p3_" + colour + ".dae");
            Skins skins = readSkins(root);
            List<float[]> vertices = new ArrayList<>();
            // "Piki_PikminCOLOR_all requires Image mapping Mirror X/Y" (notas del rip).
            for (Element g : geometries(root)) vertices.addAll(expand(g, skins, JOINTS, MIRROR, null));
            Texture tex = rgba(zip, "piki_" + colour + "_all.png");
            List<Part> parts = new ArrayList<>();
            parts.add(new Part(vertices, tex));
            writePack(new File(outDir, "piki_" + colour + ".nhm"), bonesFor(JOINTS, skins), parts);
        }
        String[][] happas = {
            { "leaf", "leaf.dae", "piki_leaf_tex.png" },
            { "bud", "bud.dae", "piki_bud_tex.png" },
            { "flower", "flower.dae", "piki_flower_tex.png" },
        };
        for (String[] h : happas) {
            Element root = dae(zip, h[1]);
            Skins skins = readSkins(root);
            String[] jointNames = skins.jointNames();
            List<float[]> vertices = new ArrayList<>();
            for (Element g : geometries(root)) vertices.addAll(expand(g, skins, jointNames, CLAMP, HAPPA_TO_JOINT));
            List<Part> parts = new ArrayList<>();
            parts.add(new Part(vertices, rgba(zip, h[2])));
            // Ya en el espacio del joint destino: inverse bind identidad.
            writePack(new File(outDir, "happa_" + h[0] + ".nhm"), identityBones(new String[] { h[0] }), parts);
        }
    }

    // ── Bulborbs ───────────────────────────────────────────────────────────

    // Los esqueletos de Pikmin 3 son distintos, así que los vértices se llevan
    // al espacio de modelo de Pikmin 1 con una transformación de semejanza
    // ajustada sobre joints equivalentes (residuos <= 2.4 unidades) y se
    // skinnean con las inverse bind del propio motor. swallow.mod se dibuja a
    // escala 3, de ahí el factor ~1/3 del grande.
    private static Transform fit(float scale, float ox, float oy, float oz) {
        return v -> new float[] { v[0] * scale + ox, v[1] * scale + oy, v[2] * scale + oz };
    }
    private static final Transform FIT_DWARF = fit(0.9463f, 0.004f, -0.07f, 1.13f);
    private static final Transform FIT_BIG = fit(0.3298f, 0.004f, 14.193f, 3.691f);

    /** Escala uniforme + traslación: las normales no cambian, solo se mueven las posiciones. */
    private static List<float[]> expandAligned(Element g, Skins skins, String[] joints, Wrap wrap, Transform move)
            throws Exception {
        List<float[]> vertices = expand(g, skins, joints, wrap, null);
        for (float[] v : vertices) {
            float[] p = move.apply(new float[] { v[0], v[1], v[2] });
            v[0] = p[0]; v[1] = p[1]; v[2] = p[2];
        }
        return vertices;
    }

    private static void buildDwarfBulborb(ZipFile zip, File outDir) throws Exception {
        Element root = dae(zip, "kochappy.dae");
        Skins skins = readSkins(root);
        String[] joints = skins.jointNames();
        // eye_3_m iris/esclerótica y eye_m brillo: discos finos a dos caras.
        // eye_2_m córnea cristalina: última, con alpha para que se vea el iris.
        List<float[]> body = new ArrayList<>(), eyes = new ArrayList<>(), moyou = new ArrayList<>(),
            cornea = new ArrayList<>();
        for (Element g : geometries(root)) {
            String m = material(g);
            List<float[]> target = "moyou_m".equals(m) ? moyou : "eye_2_m".equals(m) ? cornea
                : ("eye_3_m".equals(m) || "eye_m".equals(m)) ? eyes : body;
            target.addAll(expandAligned(g, skins, joints, CLAMP, FIT_DWARF));
        }
        Texture bodyTex = rgba(zip, "kochappy_tex.png");
        List<Part> parts = new ArrayList<>();
        parts.add(new Part(body, bodyTex));
        parts.add(new Part(eyes, bodyTex, PART_FLAG_NOCULL));
        parts.add(new Part(moyou, rgba(zip, "moyou_tex.png")));
        parts.add(new Part(cornea, bodyTex.translucent(CORNEA_ALPHA)));
        writePack(new File(outDir, "bulborb_dwarf.nhm"), identityBones(joints), parts);
    }

    private static void buildBulborb(ZipFile zip, File outDir) throws Exception {
        Element root = dae(zip, "red bulborb/model.dae");
        Skins skins = readSkins(root);
        String[] joints = skins.jointNames();

        Map<String, String> images = new HashMap<>();
        for (Element img : children(child(root, "library_images"), "image")) {
            Element init = child(img, "init_from");
            String path = init == null ? "" : init.getTextContent().trim();
            while (path.startsWith(".") || path.startsWith("/")) path = path.substring(1);
            images.put(img.getAttribute("id"), path);
        }
        Map<String, String> effectImage = new HashMap<>();
        for (Element effect : children(child(root, "library_effects"), "effect")) {
            List<Element> init = descendants(effect, "init_from");
            String img = init.isEmpty() ? "" : images.get(init.get(0).getTextContent().trim());
            effectImage.put(effect.getAttribute("id"), img == null ? "" : img);
        }
        Map<String, String> materialImage = new HashMap<>();
        for (Element mat : children(child(root, "library_materials"), "material")) {
            Element inst = child(mat, "instance_effect");
            String img = inst == null ? "" : effectImage.get(ref(inst, "url"));
            materialImage.put(mat.getAttribute("id"), img == null ? "" : img);
        }
        // <triangles material> es un símbolo que <bind_material> mapea al material real.
        Map<String, String> symbolMaterial = new HashMap<>();
        for (Element im : descendants(root, "instance_material")) {
            symbolMaterial.put(im.getAttribute("symbol"), ref(im, "target"));
        }
        // Los nodos de escena nombran las piezas en el orden de las geometrías.
        List<String> nodeNames = new ArrayList<>();
        Element scene = child(root, "library_visual_scenes", "visual_scene");
        for (Element n : children(scene, "node")) {
            if (child(n, "instance_controller") == null) continue;
            String name = n.getAttribute("name");
            nodeNames.add(name.isEmpty() ? n.getAttribute("id") : name);
        }

        List<float[]> body = new ArrayList<>(), circle = new ArrayList<>(), cornea = new ArrayList<>();
        List<Element> geoms = geometries(root);
        for (int i = 0; i < geoms.size() && i < nodeNames.size(); i++) {
            Element g = geoms.get(i);
            String symbol = material(g);
            String matId = symbolMaterial.containsKey(symbol) ? symbolMaterial.get(symbol) : symbol;
            String image = materialImage.containsKey(matId) ? materialImage.get(matId) : "face.0.png";
            List<float[]> target;
            Wrap wrap;
            if (image.startsWith("circle")) {
                // La capa de manchas repite una textura pequeña por el lomo (UVs ~ -2.3..2.2).
                target = circle; wrap = REPEAT;
            } else if (nodeNames.get(i).endsWith("eye_c_m")) {
                target = cornea; wrap = CLAMP;
            } else {
                target = body; wrap = CLAMP;
            }
            target.addAll(expandAligned(g, skins, joints, wrap, FIT_BIG));
        }
        Texture faceTex = rgba(zip, "face.0.png");
        List<Part> parts = new ArrayList<>();
        parts.add(new Part(body, faceTex));
        parts.add(new Part(circle, rgba(zip, "circle.0.png"), PART_FLAG_REPEAT));
        parts.add(new Part(cornea, faceTex.translucent(CORNEA_ALPHA)));
        writePack(new File(outDir, "bulborb.nhm"), identityBones(joints), parts);
    }
}
