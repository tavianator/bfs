everything=(%{a,b,c,d,D,f,g,G,h,H,i,k,l,m,M,n,p,P,s,S,t,u,U,y,Y})

# Check if we have fstypes
if ! fail invoke_bfs basic -printf '%F' -quit >/dev/null; then
    everything+=(%F)
fi

everything+=(%{A,C,T}{%,+,@,a,A,b,B,c,C,d,D,e,F,g,G,h,H,I,j,k,l,m,M,n,p,r,R,s,S,t,T,u,U,V,w,W,x,X,y,Y,z,Z})

# Check if we have birth times
if ! fail invoke_bfs basic -printf '%w' -quit >/dev/null; then
    everything+=(%w %{B,W}{%,+,@,a,A,b,B,c,C,d,D,e,F,g,G,h,H,I,j,k,l,m,M,n,p,r,R,s,S,t,T,u,U,V,w,W,x,X,y,Y,z,Z})
fi

invoke_bfs rainbow -printf "${everything[*]}\n" >/dev/null
