# mdtest.py - Minimal test to find syntax issues
import vibe

print("Test 1: class")
class Foo:
    def __init__(self):
        self.x = 1
    def bar(self):
        return self.x

f = Foo()
print("class works: " + str(f.bar()))

print("Test 2: string split")
s = "hello\nworld"
parts = s.split("\n")
print("split works: " + str(len(parts)))

print("Test 3: string replace")
s2 = s.replace("\n", " ")
print("replace works: " + s2)

print("Test 4: string startswith")
if "hello".startswith("he"):
    print("startswith works")

print("Test 5: tuple unpack")
t = ("a", "b")
x, y = t
print("unpack works: " + x + y)

print("Test 6: list of tuples")
lst = []
lst.append(("one", "two"))
a, b = lst[0]
print("list tuple works: " + a)

print("Test 7: for with range")
for i in range(3):
    print("i=" + str(i))

print("Test 8: chr")
c = chr(65)
print("chr works: " + c)

print("Test 9: len on string")
print("len works: " + str(len("test")))

print("All tests passed!")
