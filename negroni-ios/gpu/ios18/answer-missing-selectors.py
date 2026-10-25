from pathlib import Path
f = Path('drivers/NativeMetal/NativeBridgeMetalFrontend.mm'); s = f.read_text()
answer = (' - (NSMethodSignature *)methodSignatureForSelector:(SEL)selector {\n'
          '  NSMethodSignature *signature=[super methodSignatureForSelector:selector];if(signature)return signature;\n'
          '  if(gMissing)gMissing(class_getName(object_getClass(self)),sel_getName(selector));\n'
          '  noteProblem([NSString stringWithFormat:@"answered %s %s",class_getName(object_getClass(self)),sel_getName(selector)]);\n'
          '  return [NSMethodSignature signatureWithObjCTypes:"@@:"];\n'
          ' }\n'
          ' - (void)forwardInvocation:(NSInvocation *)invocation {id zero=nil;[invocation setReturnValue:&zero];}\n')
a = (' noteProblem([NSString stringWithFormat:@"missing %s %s",class_getName(object_getClass(self)),sel_getName(selector)]);\n'
     ' [super doesNotRecognizeSelector:selector];\n}\n')
b = (' noteProblem([NSString stringWithFormat:@"missing device %s",sel_getName(selector)]);\n'
     ' [super doesNotRecognizeSelector:selector];\n}\n')
assert s.count(a) == 1 and s.count(b) == 1, 'frontend anchors'
s = s.replace(a, a + answer).replace(b, b + answer)
f.write_text(s)

f = Path('drivers/NativeMetal/NativeAdrenoMetalPlugin.mm'); s = f.read_text()
a = 'static id deviceForPort(unsigned port) {\n'
b = '        Gpu *gpu = makeGpu(port);\n'
c = '                device = native_bridge_metal_open(turnip, candidates[i], registryID, nullptr);\n'
assert s.count(a) == 1 and s.count(b) == 1 and s.count(c) == 1, 'plugin anchors'
report = (
    'static Gpu *gMissGpu;   // iOS 18: names the selectors the frontend answers for want of an implementation\n'
    'static void reportMissing(const char *cls, const char *sel) {\n'
    '    static char seen[64][48];\n'
    '    static unsigned count;\n'
    '    char key[48];\n'
    '    snprintf(key, sizeof key, "%.20s %.26s", cls, sel);\n'
    '    for (unsigned i = 0; i < count; ++i) if (!strcmp(seen[i], key)) return;\n'
    '    if (count < 64) strlcpy(seen[count++], key, sizeof seen[0]);\n'
    '    if (!gMissGpu) return;\n'
    '    char text[64];\n'
    '    snprintf(text, sizeof text, "MISS %s", key);\n'
    '    streamText(*gMissGpu, text);\n'
    '}\n')
s = s.replace(a, report + a)
s = s.replace(b, b + '        gMissGpu = gpu;\n')
s = s.replace(c, c.replace('nullptr', 'reportMissing'))
f.write_text(s)
print('iOS 18: missing selectors answered and reported')
