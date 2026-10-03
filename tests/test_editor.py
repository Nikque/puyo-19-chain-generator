"""Independent grid simulator, with no imports from the C++ implementation."""
import pathlib,sys
root=pathlib.Path(sys.argv[1])
def read(path):
    lines=path.read_text().splitlines()
    assert lines[0]=='PUYO_BOARD 1'
    return [list(map(int,row.split())) for row in lines[1:]][::-1]
def fall(b):
    for x in range(6):
        # Each fixed wall creates a new interval. Compact within each interval.
        barriers=[-1]+[y for y in range(13) if b[y][x]==10]+[13]
        for low,high in zip(barriers,barriers[1:]):
            values=[b[y][x] for y in range(low+1,high) if b[y][x]]
            for y in range(low+1,high):b[y][x]=values[y-low-1] if y-low-1<len(values) else 0
def simulate(b):
    b=[r[:] for r in b];fall(b);score=chain=0
    dirs=((1,0),(-1,0),(0,1),(0,-1))
    while True:
        seen=set();groups=[]
        for y in range(12):
            for x in range(6):
                if (x,y) in seen or not 1<=b[y][x]<=5:continue
                group={(x,y)};pending=[(x,y)];seen.add((x,y))
                while pending:
                    px,py=pending.pop()
                    for dx,dy in dirs:
                        nx,ny=px+dx,py+dy
                        if 0<=nx<6 and 0<=ny<12 and (nx,ny) not in seen and b[ny][nx]==b[y][x]:
                            seen.add((nx,ny));group.add((nx,ny));pending.append((nx,ny))
                if len(group)>=4:groups.append(group)
        if not groups:break
        chain+=1;colored=set.union(*groups);erase=colored.copy();colors={b[y][x] for x,y in colored}
        base=len(colored)*10;point_bonus=0
        for y in range(12):
            for x in range(6):
                damage=sum((x+dx,y+dy) in colored for dx,dy in dirs)
                if not damage:continue
                kind=b[y][x]
                if kind in (6,7):erase.add((x,y));point_bonus+=50 if kind==7 else 0
                elif kind==8:
                    if damage>1:erase.add((x,y));base+=60
                    else:b[y][x]=6;base+=10
        group_bonus=sum(10 if len(g)>=11 else len(g)-3 if len(g)>=5 else 0 for g in groups)
        chain_bonus=([0,8,16,32]+list(range(64,513,32)))[chain-1]
        score+=base*max(1,chain_bonus+[0,0,3,6,12,24][len(colors)]+group_bonus)+point_bonus
        for x,y in erase:b[y][x]=0
        fall(b)
    return b,chain,score
for i in range(500):
    final,chain,score=simulate(read(root/f'{i}.puyoboard'))
    assert final==read(root/f'{i}.final'),i
    assert [chain,score]==list(map(int,(root/f'{i}.stats').read_text().split())),i
print('PASS independent simulation of 500 boards: specials, walls, row13, chains and scores')
