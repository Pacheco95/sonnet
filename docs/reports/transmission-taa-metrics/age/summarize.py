import csv,sys,os
d=sys.argv[1]
for c in sorted(os.listdir(d)):
    f=os.path.join(d,c,'metrics.csv')
    if not os.path.exists(f): continue
    r=list(csv.DictReader(open(f)))
    m=max(float(x['glass_mean']) for x in r); p=max(int(x['glass_p99']) for x in r)
    u=max(int(x['unrecovered']) for x in r); ps=sum(int(x['pass']) for x in r)
    om=max(float(x['opaque_mean']) for x in r); op=max(int(x['opaque_p99']) for x in r)
    print(f"{c:26s} mean {m:5.2f} p99 {p:3d} unrec {u:5d} pass {ps}/{len(r)} opaque {om:.2f}/{op}")
