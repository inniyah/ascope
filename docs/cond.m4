.PS

# A signal conditioning circuit

cct_init

# input
reversed(`tconn',right_,<)
{ resistor(down_); llabel(,`1M',); ground }

# input protection
resistor(right_); llabel(,`10k',)

# voltage follower
N:Here
line right_ elen_/4
A:opamp(,,,,R) with .In1 at Here; llabel(,`"\tt TL072"',)
line down_ elen_/2
line to (N,Here) then to (N,A.In2) then to A.In2

# 2:1 divider
line from A.Out right_ elen_/2
line up_ then right_ elen_/3
resistor(down_); llabel(,`10k',)
{ resistor(down_); llabel(,`10k',); ground }

# summator
line right_ elen_/2
resistor(right_); rlabel(,`100k',)
d=distance(A.In1,A.In2)/2
line up_ d
N2:Here
{
line up_ d
resistor(left_); rlabel(,`100k',)
tconn(up_,O); "+2.5V" at last line.end above
}
line right_ elen_/4
A2:opamp(,,,,R) with .In1 at Here; llabel(,`"\tt TL072"',)
line down_
resistor(left_ to (N2,Here)); rlabel(,`10k',)
{ line to (N2,A2.In2) then to A2.In2 }
resistor(down_); llabel(,`10k',)
ground

# output protection
resistor(right_ from A2.Out); llabel(,`10k',)

# output
tconn(right_)

.PE
