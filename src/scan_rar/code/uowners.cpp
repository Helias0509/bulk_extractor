

void ExtractUnixOwner(Archive &Arc,char *FileName)
{
  if (Arc.HeaderCRC!=Arc.UOHead.HeadCRC)
  {
    ErrHandler.SetErrorCode(CRC_ERROR);
    return;
  }

  struct passwd *pw;
  errno=0; // Required by getpwnam specification if we need to check errno.
  if ((pw=getpwnam(Arc.UOHead.OwnerName))==NULL)
  {
    ErrHandler.SysErrMsg();
    ErrHandler.SetErrorCode(WARNING);
    return;
  }
  uid_t OwnerID=pw->pw_uid;

  struct group *gr;
  errno=0; // Required by getgrnam specification if we need to check errno.
  if ((gr=getgrnam(Arc.UOHead.GroupName))==NULL)
  {
    ErrHandler.SysErrMsg();
    ErrHandler.SetErrorCode(CRC_ERROR);
    return;
  }
  uint Attr=GetFileAttr(FileName,NULL);
  gid_t GroupID=gr->gr_gid;
#if defined(SAVE_LINKS) && !defined(_APPLE)
  if (lchown(FileName,OwnerID,GroupID)!=0)
#else
  if (chown(FileName,OwnerID,GroupID)!=0)
#endif
  {
    ErrHandler.SetErrorCode(CREATE_ERROR);
  }
  SetFileAttr(FileName,NULL,Attr);
}


void ExtractUnixOwnerNew(Archive &Arc,char *FileName)
{
  char *OwnerName=(char *)&Arc.SubHead.SubData[0];
  int OwnerSize=strlen(OwnerName)+1;
  int GroupSize=Arc.SubHead.SubData.Size()-OwnerSize;
  char GroupName[NM];
  strncpy(GroupName,(char *)&Arc.SubHead.SubData[OwnerSize],GroupSize);
  GroupName[GroupSize]=0;

  struct passwd *pw;
  if ((pw=getpwnam(OwnerName))==NULL)
  {
    ErrHandler.SetErrorCode(WARNING);
    return;
  }
  uid_t OwnerID=pw->pw_uid;

  struct group *gr;
  if ((gr=getgrnam(GroupName))==NULL)
  {
    ErrHandler.SetErrorCode(CRC_ERROR);
    return;
  }
  uint Attr=GetFileAttr(FileName,NULL);
  gid_t GroupID=gr->gr_gid;
#if defined(SAVE_LINKS) && !defined(_APPLE)
  if (lchown(FileName,OwnerID,GroupID)!=0)
#else
  if (chown(FileName,OwnerID,GroupID)!=0)
#endif
  {
    ErrHandler.SetErrorCode(CREATE_ERROR);
  }
  SetFileAttr(FileName,NULL,Attr);
}
