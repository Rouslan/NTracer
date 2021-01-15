import unittest
import random

from ..wrapper import NTracer,CUBE,SPHERE
from ..render import Material,Color

def pydot(a,b):
    return sum(ia*ib for ia,ib in zip(a,b))

def and_generic(f):
    def inner(self):
        f(self,False)
        f(self,True)
    return inner

class Tests(unittest.TestCase):
    def test_simd(self):
        d = 64
        while d > 4:
            nt = NTracer(d)
            a = nt.Vector(range(d))
            b = nt.Vector(x+12 for x in range(d-1,-1,-1))
            self.assertAlmostEqual(nt.dot(a,b),pydot(a,b))

            d = d >> 1

    @and_generic
    def test_math(self,generic):
        nt = NTracer(4,generic)
        ma = nt.Matrix([[10,2,3,4],[5,6,7,8],[9,10,11,12],[13,14,15,16]])
        mb = nt.Matrix([13,6,9,6,7,3,3,13,1,11,12,7,12,15,17,15])
        mx = ma * mb
        my = nt.Matrix([195,159,200,167,210,245,283,277,342,385,447,441,474,525,611,605])

        self.assertEqual(mx,my)
        for vx,vy in zip(mx.values,my.values):
            self.assertEqual(vx,vy)

        for a,b in zip((mb * mb.inverse()).values,[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]):
            self.assertAlmostEqual(a,b,4)

        for a,b in zip(nt.Vector(13,2,16,14).unit(),[0.52,0.08,0.64,0.56]):
            self.assertAlmostEqual(a,b,4)

    @and_generic
    def test_aabb(self,generic):
        nt = NTracer(5,generic)
        a = nt.AABB((1,7,-5,5,4),(5,13,-1,6,12))
        self.assertEqual(a.dimension,5)
        self.assertEqual(list(a.end),[5,13,-1,6,12])
        self.assertEqual(list(a.start),[1,7,-5,5,4])
        self.assertEqual(list(a.right(2,-3).start),[1,7,-3,5,4])
        self.assertEqual(list(a.left(0,2).end),[2,13,-1,6,12])

    @and_generic
    def test_triangle(self,generic):
        nt = NTracer(3,generic)
        mat = Material((1,1,1))
        box = nt.AABB((-1,-1,-1),(1,1,1))

        self.assertFalse(box.intersects(nt.TrianglePrototype([
            (-2.092357,0.1627209,0.9231308),
            (0.274588,0.8528936,2.309217),
            (-1.212236,1.855952,0.3137006)],mat)))

        self.assertFalse(box.intersects(nt.TrianglePrototype([
            (2.048058,-3.022543,1.447644),
            (1.961913,-0.5438575,-0.1552723),
            (0.3618142,-1.684767,0.2162201)],mat)))

        self.assertFalse(box.intersects(nt.TrianglePrototype([
            (-4.335572,-1.690142,-1.302721),
            (0.8976227,0.5090631,4.6815),
            (-0.8176082,4.334341,-1.763081)],mat)))

        self.assertTrue(box.intersects(nt.TrianglePrototype([
            (0,0,0),
            (5,5,5),
            (1,2,3)],mat)))

        self.assertTrue(nt.AABB(
            (-0.894424974918,-1.0,-0.850639998913),
            (0.0,-0.447214990854,0.850639998913)).intersects(
                nt.TrianglePrototype([
                    (0.0,-1.0,0.0),
                    (0.723599970341,-0.447214990854,0.525720000267),
                    (-0.276385009289,-0.447214990854,0.850639998913)],mat)))

    @and_generic
    def test_cube(self,generic):
        nt = NTracer(3,generic)
        mat = Material((1,1,1))
        box = nt.AABB((-1,-1,-1),(1,1,1))

        self.assertFalse(box.intersects(nt.SolidPrototype(
            CUBE,
            nt.Vector(1.356136,1.717844,1.577731),
            nt.Matrix(-0.01922399,-0.3460019,0.8615935,
                      -0.03032121,-0.6326356,-0.5065715,
                      0.03728577,-0.6928598,0.03227519),
            mat)))

        self.assertFalse(box.intersects(nt.SolidPrototype(
            CUBE,
            nt.Vector(1.444041,1.433598,1.975453),
            nt.Matrix(0.3780299,-0.3535482,0.8556266,
                      -0.7643852,-0.6406123,0.07301452,
                      0.5223108,-0.6816301,-0.5124177),
            mat)))

        self.assertFalse(box.intersects(nt.SolidPrototype(
            CUBE,
            nt.Vector(-0.31218,-3.436678,1.473133),
            nt.Matrix(0.8241131,-0.2224413,1.540015,
                      -1.461101,-0.7099018,0.6793453,
                      0.5350775,-1.595884,-0.516849),
            mat)))

        self.assertFalse(box.intersects(nt.SolidPrototype(
            CUBE,
            nt.Vector(0.7697315,-3.758033,1.847144),
            nt.Matrix(0.6002195,-1.608681,-0.3900863,
                      -1.461104,-0.7098908,0.6793506,
                      -0.7779449,0.0921175,-1.576897),
            mat)))

        self.assertTrue(box.intersects(nt.SolidPrototype(
            CUBE,
            nt.Vector(0.4581598,-1.56134,0.5541568),
            nt.Matrix(0.3780299,-0.3535482,0.8556266,
                      -0.7643852,-0.6406123,0.07301452,
                      0.5223108,-0.6816301,-0.5124177),
            mat)))

    @and_generic
    def test_sphere(self,generic):
        nt = NTracer(3,generic)
        mat = Material((1,1,1))
        box = nt.AABB((-1,-1,-1),(1,1,1))

        self.assertFalse(box.intersects(nt.SolidPrototype(
            SPHERE,
            nt.Vector(-1.32138,1.6959,1.729396),
            nt.Matrix.identity(),
            mat)))

        self.assertTrue(box.intersects(nt.SolidPrototype(
            SPHERE,
            nt.Vector(1.623511,-1.521197,-1.243952),
            nt.Matrix.identity(),
            mat)))

    @and_generic
    def test_batch_interface(self,generic):
        nt = NTracer(4,generic)
        mat = Material((1,1,1))

        lo = lambda: random.uniform(-1,1)
        hi = lambda: random.uniform(9,11)

        protos = []
        for i in range(nt.BATCH_SIZE):
            protos.append(nt.TrianglePrototype([
                (lo(),lo(),lo(),lo()),
                (lo(),hi(),lo(),lo()),
                (hi(),lo(),lo(),lo()),
                (lo(),lo(),hi(),lo())],Material((1,1,1.0/(i+1)))))

        bproto = nt.TriangleBatchPrototype(protos)
        for i in range(nt.BATCH_SIZE):
            self.assertEqual(protos[i].face_normal,bproto.face_normal[i])
            for j in range(nt.dimension):
                self.assertEqual(protos[i].point_data[j].point,bproto.point_data[j].point[i])
                self.assertEqual(protos[i].point_data[j].edge_normal,bproto.point_data[j].edge_normal[i])
            self.assertEqual(protos[i].material,bproto.material[i])

    @and_generic
    def test_buffer_interface(self,generic):
        nt = NTracer(7,generic)
        v = nt.Vector(1,2,3,4,5,6,7)
        self.assertEqual(list(v),list(memoryview(v)))

        c = Color(0.5,0.1,0)
        self.assertEqual(list(c),list(memoryview(c)))

    @and_generic
    def test_kdtree(self,generic):
        nt = NTracer(3,generic)
        mat = Material((1,1,1))
        primitives = [
          nt.Triangle(
            (-1.1755770444869995,0.3819499611854553,-1.6180520057678223),
            (1.7082732915878296,-2.3512351512908936,1.4531432390213013),
            [(-0.615524172782898,-0.3236003816127777,0.19999605417251587),
             (0.49796950817108154,0.0381958931684494,-0.5235964059829712)],mat),
          nt.Triangle(
            (-1.1755770444869995,0.3819499611854553,-1.6180520057678223),
            (1.0557708740234375,-1.4531433582305908,0.8980922102928162),
            [(-0.8057316541671753,-0.06180214881896973,0.8471965789794922),
             (0.19020742177963257,-0.2617982029914856,-0.6472004652023315)],mat),
          nt.Triangle(
            (0.7265498042106628,0.9999955296516418,1.6180428266525269),
            (0,1.7961481809616089,0.8980742692947388),
            [(-1.1135050058364868,-0.1618017703294754,0.32360348105430603),
             (0.6881839036941528,-0.09999901801347733,0.19999800622463226)],mat),
          nt.Triangle(
            (0.7265498042106628,0.9999955296516418,1.6180428266525269),
            (0,2.90622878074646,1.4531147480010986),
            [(-0.4253210127353668,-0.26180076599121094,0.5236014127731323),
             (0.6881839036941528,0.09999898821115494,-0.1999979317188263)],mat),
          nt.Triangle(
            (1.9021340608596802,0.618022620677948,-0.3819592595100403),
            (-1.055770754814148,-1.4531432390213013,0.8980920910835266),
            [(-0.30776214599609375,-0.42359834909439087,-1.0471925735473633),
             (0.4979696571826935,-0.038195837289094925,0.5235962867736816)],mat),
          nt.Triangle(
            (1.9021340608596802,0.618022620677948,-0.3819592595100403),
            (-1.7082730531692505,-2.3512353897094727,1.4531434774398804),
            [(0.19020749628543854,-0.4617941677570343,-0.5235962271690369),
             (0.19020745158195496,0.2617981433868408,0.6472005844116211)],mat)]
        scene = nt.CompositeScene(
          nt.AABB(
            (-1.710653305053711e-05,0.618022620677948,-0.3819774389266968),
            (0.7265291213989258,2.000016689300537,0.3819882869720459)),
          nt.KDBranch(1,2.0000057220458984,
            nt.KDBranch(1,0.9999955296516418,
              None,
              nt.KDLeaf([
                primitives[4],
                primitives[5],
                primitives[2],
                primitives[3],
                primitives[1],
                primitives[0]])),
            nt.KDLeaf([
              primitives[4],
              primitives[5],
              primitives[1],
              primitives[0]])))
        scene.set_fov(0.8)
        hits = scene.root.intersects(
          (4.917067527770996,2.508934497833252,-4.304379940032959),
          (-0.7135500907897949,-0.1356230527162552,0.6873518228530884),
        )
        self.assertEqual(len(hits),1)
        self.assertEqual(primitives.index(hits[0].primitive),4)
        self.assertEqual(hits[0].batch_index,-1)


if __name__ == '__main__':
    unittest.main()
