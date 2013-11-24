import unittest

from ..wrapper import NTracer,CUBE,SPHERE

class Intersection(unittest.TestCase):
    def test_triangle(self):
        nt = NTracer(3)
        box = nt.AABB((-1,-1,-1),(1,1,1))
        
        self.assertFalse(box.intersects(nt.TrianglePrototype([
            (-2.092357,0.1627209,0.9231308),
            (0.274588,0.8528936,2.309217),
            (-1.212236,1.855952,0.3137006)])))

        self.assertFalse(box.intersects(nt.TrianglePrototype([
            (2.048058,-3.022543,1.447644),
            (1.961913,-0.5438575,-0.1552723),
            (0.3618142,-1.684767,0.2162201)])))

        self.assertFalse(box.intersects(nt.TrianglePrototype([
            (-4.335572,-1.690142,-1.302721),
            (0.8976227,0.5090631,4.6815),
            (-0.8176082,4.334341,-1.763081)])))
        
        self.assertTrue(box.intersects(nt.TrianglePrototype([
            (0,0,0),
            (5,5,5),
            (1,2,3)])))
        
    def test_cube(self):
        nt = NTracer(3)
        box = nt.AABB((-1,-1,-1),(1,1,1))
        
        self.assertFalse(box.intersects(nt.CubePrototype(
            nt.Matrix(-0.01922399,-0.3460019,0.8615935,-0.03032121,-0.6326356,-0.5065715,0.03728577,-0.6928598,0.03227519),
            nt.Vector(1.356136,1.717844,1.577731))))

        self.assertFalse(box.intersects(nt.CubePrototype(
            nt.Matrix(0.3780299,-0.3535482,0.8556266,-0.7643852,-0.6406123,0.07301452,0.5223108,-0.6816301,-0.5124177),
            nt.Vector(1.444041,1.433598,1.975453))))

        self.assertFalse(box.intersects(nt.CubePrototype(
            nt.Matrix(0.8241131,-0.2224413,1.540015,-1.461101,-0.7099018,0.6793453,0.5350775,-1.595884,-0.516849),
            nt.Vector(-0.31218,-3.436678,1.473133))))

        self.assertFalse(box.intersects(nt.CubePrototype(
            nt.Matrix(0.6002195,-1.608681,-0.3900863,-1.461104,-0.7098908,0.6793506,-0.7779449,0.0921175,-1.576897),
            nt.Vector(0.7697315,-3.758033,1.847144))))

        self.assertTrue(box.intersects(nt.CubePrototype(
            nt.Matrix(0.3780299,-0.3535482,0.8556266,-0.7643852,-0.6406123,0.07301452,0.5223108,-0.6816301,-0.5124177),
            nt.Vector(0.4581598,-1.56134,0.5541568))))
        
    def test_sphere(self):
        nt = NTracer(3)
        box = nt.AABB((-1,-1,-1),(1,1,1))
        
        self.assertFalse(box.intersects(nt.Solid(
            SPHERE,
            nt.Matrix.identity(),
            nt.Vector(-1.32138,1.6959,1.729396))))
        
        self.assertTrue(box.intersects(nt.Solid(
            SPHERE,
            nt.Matrix.identity(),
            nt.Vector(1.623511,-1.521197,-1.243952))))

